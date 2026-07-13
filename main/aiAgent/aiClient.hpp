#pragma once

#include <functional>
#include <string>
#include <esp_http_client.h>
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

        // response_format — json_schema 约束
        cJSON* respFmt = cJSON_CreateObject();
        cJSON* jsonSchema = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonSchema, "name", "actions");
        cJSON_AddBoolToObject(jsonSchema, "strict", false);

        cJSON* schema = cJSON_CreateObject();
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON* props = cJSON_AddObjectToObject(schema, "properties");
        cJSON* actionsArr = cJSON_CreateObject();
        cJSON_AddStringToObject(actionsArr, "type", "array");
        cJSON* items = cJSON_AddObjectToObject(actionsArr, "items");
        cJSON_AddStringToObject(items, "type", "object");
        cJSON* itemProps = cJSON_AddObjectToObject(items, "properties");
        auto addStrProp = [&](const char* name) {
            cJSON* p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddItemToObject(itemProps, name, p);
        };
        addStrProp("action");
        addStrProp("text");
        addStrProp("app");
        addStrProp("command");
        cJSON* valProp = cJSON_CreateObject();
        cJSON_AddStringToObject(valProp, "type", "integer");
        cJSON_AddItemToObject(itemProps, "value", valProp);
        cJSON* paramsProp = cJSON_CreateObject();
        cJSON_AddStringToObject(paramsProp, "type", "object");
        cJSON_AddItemToObject(itemProps, "params", paramsProp);

        cJSON* required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("action"));
        cJSON_AddItemToObject(items, "required", required);

        cJSON_AddItemToObject(props, "actions", actionsArr);
        cJSON* rootRequired = cJSON_CreateArray();
        cJSON_AddItemToArray(rootRequired, cJSON_CreateString("actions"));
        cJSON_AddItemToObject(schema, "required", rootRequired);
        cJSON_AddItemToObject(jsonSchema, "schema", schema);
        cJSON_AddItemToObject(respFmt, "json_schema", jsonSchema);
        cJSON_AddStringToObject(respFmt, "type", "json_schema");
        cJSON_AddItemToObject(root, "response_format", respFmt);

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
            cfg.buffer_size = 8192;
            cfg.buffer_size_tx = 1024;
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

        if (contentLen <= 0) contentLen = 8192;  // chunked fallback

        // 5. 读取响应
        respBuf = static_cast<char*>(heap_caps_malloc(contentLen + 1, MALLOC_CAP_SPIRAM));
        if (!respBuf) {
            errorMsg = "内存不足";
            goto cleanup;
        }

        {
            int totalRead = esp_http_client_read_response(client, respBuf, contentLen);
            if (totalRead > 0) {
                respBuf[totalRead] = '\0';
                responseText.assign(respBuf, totalRead);
                success = true;
                ESP_LOGI(TAG, "读取 %d 字节", totalRead);
                ESP_LOGI(TAG, "响应体: %s", respBuf);
            } else {
                errorMsg = (totalRead < 0) ? "读取响应失败" : "响应体为空";
                ESP_LOGW(TAG, "%s (ret=%d)", errorMsg.c_str(), totalRead);
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
