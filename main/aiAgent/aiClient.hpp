#pragma once

#include <functional>
#include <string>
#include <errno.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <cJSON.h>
#include "aiProtocol.hpp"
#include "aiConfig.hpp"
#include "task/task.hpp"
#include <vector>
#include <utility>

/**
 * @file aiClient.hpp
 * @brief AI HTTP 客户端
 *
 * 封装与 OpenAI 兼容 API 的 HTTPS 通信。
 * 使用 ESP-IDF 内置 esp_http_client。
 */

// ============================================================
//  回调类型
// ============================================================

using AiClientCallback = std::function<void(bool success, const std::string& responseText, const std::string& errorMsg)>;

// ============================================================
//  HttpClient
// ============================================================

class HttpClient {
public:
    constexpr static char TAG[] = "AiClient";

    /** @brief 简单发送：system + user 两条消息 */
    static bool send(const AiConfig& config,
                     const std::string& systemPrompt,
                     const std::string& userInput,
                     AiClientCallback callback)
    {
        return sendWithHistory(config, systemPrompt, userInput, {}, std::move(callback));
    }

    /** @brief 带历史消息的发送：system + history + user */
    static bool sendWithHistory(const AiConfig& config,
                                const std::string& systemPrompt,
                                const std::string& userInput,
                                const std::vector<std::pair<std::string, std::string>>& history,
                                AiClientCallback callback)
    {
        auto* ctx = new SendContext{ config, systemPrompt, userInput, history, std::move(callback) };
        Task::addTask([](void* param) -> TickType_t {
            auto* ctx = static_cast<SendContext*>(param);
            executeRequest(*ctx);
            delete ctx;
            return Task::infinityTime;
        }, "aiClient", ctx, 0, Task::Affinity::None);
        return true;
    }

private:
    struct SendContext {
        AiConfig config;
        std::string systemPrompt;
        std::string userInput;
        std::vector<std::pair<std::string, std::string>> history;
        AiClientCallback callback;
    };

    static void executeRequest(const SendContext& ctx) {
        std::string responseText;
        std::string errorMsg;
        bool success = false;

        // 1. 拼接请求体 JSON
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "model", ctx.config.model.c_str());

        // messages
        cJSON* messages = cJSON_AddArrayToObject(root, "messages");

        // system
        cJSON* sysMsg = cJSON_CreateObject();
        cJSON_AddStringToObject(sysMsg, "role", "system");
        cJSON_AddStringToObject(sysMsg, "content", ctx.systemPrompt.c_str());
        cJSON_AddItemToArray(messages, sysMsg);

        // history (user/assistant 交替)
        for (const auto& [role, content] : ctx.history) {
            cJSON* msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "role", role.c_str());
            cJSON_AddStringToObject(msg, "content", content.c_str());
            cJSON_AddItemToArray(messages, msg);
        }

        // current user input
        cJSON* userMsg = cJSON_CreateObject();
        cJSON_AddStringToObject(userMsg, "role", "user");
        cJSON_AddStringToObject(userMsg, "content", ctx.userInput.c_str());
        cJSON_AddItemToArray(messages, userMsg);

        cJSON_AddNumberToObject(root, "temperature", ctx.config.temperature);
        cJSON_AddNumberToObject(root, "max_tokens", ctx.config.maxTokens);

        char* jsonStr = cJSON_PrintUnformatted(root);
        std::string body(jsonStr);
        cJSON_Delete(root);
        cJSON_free(jsonStr);

        // ── 日志：请求体 ──
        ESP_LOGI(TAG, "请求体(%zu 字节): %s", body.size(), body.c_str());

        // 2. 构建 URL
        std::string url = ctx.config.apiUrl;
        if (!url.empty() && url.back() == '/') url.pop_back();
        url += "/v1/chat/completions";

        // 3. 配置 esp_http_client
        esp_http_client_handle_t client = nullptr;
        char* respBuf = nullptr;
        int contentLen = 0;
        int status = 0;

        {
            esp_http_client_config_t cfg = {};
            cfg.url = url.c_str();
            cfg.method = HTTP_METHOD_POST;
            cfg.timeout_ms = ctx.config.timeoutMs;
            cfg.buffer_size = 40960;
            cfg.buffer_size_tx = 1024;
            cfg.crt_bundle_attach = esp_crt_bundle_attach;
            client = esp_http_client_init(&cfg);
        }

        if (!client) {
            errorMsg = "esp_http_client_init 失败";
            goto cleanup;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "Authorization",
                                    ("Bearer " + ctx.config.apiKey).c_str());

        // 4. 使用 open/write/fetch_headers 模式（perform 会消费 body 导致无法读取）
        ESP_LOGI(TAG, "POST %s", url.c_str());
        esp_http_client_open(client, body.size());
        esp_http_client_write(client, body.data(), body.size());

        contentLen = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP %d, Content-Length: %d", status, contentLen);

        if (status != 200) {
            errorMsg = "API 返回 ";
            errorMsg += std::to_string(status);
            goto cleanup;
        }

        // 5. 读取响应（支持 chunked，动态扩容）
        {
            size_t bufSize = (contentLen > 0) ? (size_t)contentLen + 1 : 32768;
            size_t offset = 0;
            int readLen = 0;

            respBuf = static_cast<char*>(heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM));
            if (!respBuf) {
                errorMsg = "内存不足";
                goto cleanup;
            }

            while ((readLen = esp_http_client_read(client, respBuf + offset,
                                                     bufSize - offset - 1)) > 0) {
                offset += readLen;
                respBuf[offset] = '\0';

                // 如果缓冲区接近用完，翻倍扩容
                if (bufSize - offset < 4096) {
                    size_t newSize = bufSize * 2;
                    char* newBuf = static_cast<char*>(
                        heap_caps_realloc(respBuf, newSize, MALLOC_CAP_SPIRAM));
                    if (!newBuf) {
                        ESP_LOGW(TAG, "响应过大(%zu+)，已读取 %zu 字节", bufSize, offset);
                        break;  // 内存不足，返回已读取部分
                    }
                    respBuf = newBuf;
                    bufSize = newSize;
                }
            }

            if (offset > 0) {
                responseText.assign(respBuf, offset);
                success = true;
                ESP_LOGI(TAG, "读取 %zu 字节 (缓冲 %zu)", offset, bufSize);
                ESP_LOGI(TAG, "响应体: %s", respBuf);
            } else if (contentLen > 0 && readLen == 0) {
                // fallback: 用 read_response（Content-Length 已知且为 EOF 时）
                // 注意：readLen < 0（超时/错误）时不再走此路径，避免再白等一个超时周期
                respBuf = static_cast<char*>(
                    heap_caps_malloc(contentLen + 1, MALLOC_CAP_SPIRAM));
                if (respBuf) {
                    int totalRead = esp_http_client_read_response(client, respBuf, contentLen);
                    if (totalRead > 0) {
                        respBuf[totalRead] = '\0';
                        responseText.assign(respBuf, totalRead);
                        success = true;
                        ESP_LOGI(TAG, "读取 %d 字节 (fallback)", totalRead);
                    }
                }
            }

            if (!success) {
                if (readLen < 0) {
                    // 读超时或 socket 错误（最常见：生成耗时超过配置超时）
                    errorMsg = "响应读取超时或中断（配置超时 ";
                    errorMsg += std::to_string(ctx.config.timeoutMs / 1000);
                    errorMsg += "s）";
                    ESP_LOGW(TAG, "响应读取失败: readLen=%d errno=%d", readLen, errno);
                } else {
                    errorMsg = "响应体为空（HTTP 200 无正文）";
                    ESP_LOGW(TAG, "%s", errorMsg.c_str());
                }
            }
        }

    cleanup:
        if (respBuf) free(respBuf);
        if (client) esp_http_client_cleanup(client);

        if (!success && errorMsg.empty())
            errorMsg = "未知错误";

        if (ctx.callback) {
            ctx.callback(success, responseText, errorMsg);
        }
    }
};
