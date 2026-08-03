#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include "task/task.hpp"

/**
 * @file marketClient.hpp
 * @brief 游戏市场 HTTP 客户端
 *
 * 异步（Task 中执行）访问市场服务器的三个只读接口：
 *   GET /api/scripts      — 脚本列表（JSON 数组）
 *   GET <download 相对路径> — 脚本内容（Lua 文本）
 *   GET <screenshot 相对路径> — 截图图片（写入文件）
 *
 * 所有请求在后台 Task 中执行，通过回调通知结果，不阻塞 LVGL。
 * 与后端形态解耦：设备端只依赖上述 GET 协议。
 */

// ============================================================
//  数据结构
// ============================================================

struct MarketScript {
    std::string id;          // 唯一标识（用于文件名/下载 URL）
    std::string name = "未命名";
    std::string author;
    std::string version;
    int    sizeBytes{0};
    float  rating{0};
    int    downloads{0};
    std::string updated;     // 更新时间（字符串）
    std::string desc;        // 简介
    std::string thumb;       // 缩略图 URL（卡片小图，相对或绝对，可选）
    std::string screenshot;  // 大图 URL（详情，相对或绝对）
    std::string download;    // 下载 URL（相对或绝对），缺省 /scripts/<id>.lua
};

// ============================================================
//  回调类型
// ============================================================

using MarketListCallback = std::function<void(bool success, std::vector<MarketScript> list, const std::string& errorMsg)>;
using MarketTextCallback = std::function<void(bool success, const std::string& content, const std::string& errorMsg)>;
using MarketFileCallback = std::function<void(bool success, const std::string& errorMsg)>;

// ============================================================
//  MarketClient
// ============================================================

class MarketClient {
public:
    constexpr static char TAG[] = "MarketClient";

    /** @brief 拉取脚本列表（异步） */
    static void fetchList(const std::string& baseUrl, MarketListCallback cb)
    {
        auto* ctx = new ListCtx{ baseUrl, std::move(cb) };
        Task::addTask([](void* param) -> TickType_t {
            auto* c = static_cast<ListCtx*>(param);
            executeFetchList(*c);
            delete c;
            return Task::infinityTime;
        }, "marketList", ctx, 0, Task::Affinity::None);
    }

    /** @brief 下载脚本内容为文本（异步） */
    static void downloadScriptText(const std::string& baseUrl, const std::string& relPath, MarketTextCallback cb)
    {
        auto* ctx = new TextCtx{ baseUrl, relPath, std::move(cb) };
        Task::addTask([](void* param) -> TickType_t {
            auto* c = static_cast<TextCtx*>(param);
            executeDownloadText(*c);
            delete c;
            return Task::infinityTime;
        }, "marketScript", ctx, 0, Task::Affinity::None);
    }

    /** @brief 下载文件到目标路径（异步，用于截图缓存） */
    static void downloadFile(const std::string& baseUrl, const std::string& relPath, const std::string& destPath, MarketFileCallback cb)
    {
        auto* ctx = new FileCtx{ baseUrl, relPath, destPath, std::move(cb) };
        Task::addTask([](void* param) -> TickType_t {
            auto* c = static_cast<FileCtx*>(param);
            executeDownloadFile(*c);
            delete c;
            return Task::infinityTime;
        }, "marketFile", ctx, 0, Task::Affinity::None);
    }

    /** @brief 上报一次下载（POST /api/stats/download，异步、尽力而为，不阻塞） */
    static void reportDownload(const std::string& baseUrl, const std::string& id)
    {
        auto* ctx = new DownloadReportCtx{ baseUrl, id };
        Task::addTask([](void* param) -> TickType_t {
            auto* c = static_cast<DownloadReportCtx*>(param);
            executeReportDownload(*c);
            delete c;
            return Task::infinityTime;
        }, "marketReport", ctx, 0, Task::Affinity::None);
    }

private:
    // ── 上下文 ──
    struct ListCtx { std::string baseUrl; MarketListCallback cb; };
    struct TextCtx { std::string baseUrl, relPath; MarketTextCallback cb; };
    struct FileCtx { std::string baseUrl, relPath, destPath; MarketFileCallback cb; };
    struct DownloadReportCtx { std::string baseUrl, id; };

    // ── URL 拼接 ──
    static std::string joinUrl(const std::string& base, const std::string& path)
    {
        if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0)
            return path;
        std::string b = base;
        while (!b.empty() && b.back() == '/') b.pop_back();
        if (path.empty()) return b;
        return b + (path[0] == '/' ? path : "/" + path);
    }

    // ── 阻塞 GET 读取响应体（在 Task 中调用） ──
    static bool httpGetBody(const std::string& url, int timeoutMs, size_t maxBytes,
                            std::string& outBody, int& outStatus, std::string& errorMsg)
    {
        bool isHttps = (url.rfind("https://", 0) == 0);

        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.method = HTTP_METHOD_GET;
        cfg.timeout_ms = timeoutMs;
        cfg.buffer_size = 8192;
        cfg.buffer_size_tx = 1024;
        if (isHttps) cfg.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            errorMsg = "esp_http_client_init 失败";
            return false;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            errorMsg = "连接失败: ";
            errorMsg += esp_err_to_name(err);
            esp_http_client_cleanup(client);
            return false;
        }

        int contentLen = esp_http_client_fetch_headers(client);
        outStatus = esp_http_client_get_status_code(client);
        if (outStatus < 200 || outStatus >= 300) {
            errorMsg = "HTTP ";
            errorMsg += std::to_string(outStatus);
            esp_http_client_cleanup(client);
            return false;
        }

        // 动态读取（支持 chunked）
        size_t bufSize = (contentLen > 0) ? (size_t)contentLen + 1 : 16384;
        if (bufSize > maxBytes) bufSize = maxBytes;
        if (bufSize < 16384) bufSize = 16384;
        char* buf = static_cast<char*>(heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM));
        if (!buf) {
            errorMsg = "内存不足";
            esp_http_client_cleanup(client);
            return false;
        }

        size_t offset = 0;
        int readLen = 0;
        while ((readLen = esp_http_client_read(client, buf + offset, bufSize - offset - 1)) > 0) {
            offset += readLen;
            buf[offset] = '\0';
            if (bufSize - offset < 4096) {
                if (bufSize >= maxBytes) break;   // 已达上限
                size_t newSize = bufSize * 2;
                if (newSize > maxBytes) newSize = maxBytes;
                char* nb = static_cast<char*>(heap_caps_realloc(buf, newSize, MALLOC_CAP_SPIRAM));
                if (!nb) break;
                buf = nb;
                bufSize = newSize;
            }
        }

        bool ok = (offset > 0);
        if (ok) {
            outBody.assign(buf, offset);
        } else if (readLen < 0) {
            errorMsg = "响应读取超时或中断";
        } else {
            errorMsg = "响应体为空";
        }
        free(buf);
        esp_http_client_cleanup(client);
        return ok;
    }

    // ── 阻塞 POST JSON（在 Task 中调用，尽力而为） ──
    static bool httpPostJson(const std::string& url, const std::string& jsonBody, int timeoutMs,
                             std::string& outBody, int& outStatus, std::string& errorMsg)
    {
        bool isHttps = (url.rfind("https://", 0) == 0);

        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.method = HTTP_METHOD_POST;
        cfg.timeout_ms = timeoutMs;
        cfg.buffer_size = 2048;
        cfg.buffer_size_tx = 512;
        if (isHttps) cfg.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            errorMsg = "esp_http_client_init 失败";
            return false;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json");

        esp_err_t err = esp_http_client_open(client, jsonBody.size());
        if (err != ESP_OK) {
            errorMsg = esp_err_to_name(err);
            esp_http_client_cleanup(client);
            return false;
        }
        if (esp_http_client_write(client, jsonBody.data(), jsonBody.size()) < 0) {
            errorMsg = "写入请求体失败";
            esp_http_client_cleanup(client);
            return false;
        }

        int contentLen = esp_http_client_fetch_headers(client);
        outStatus = esp_http_client_get_status_code(client);

        // 尽量读少量响应体（统计接口返回小 JSON）
        if (outStatus >= 200 && outStatus < 300 && contentLen > 0) {
            char buf[512];
            int n = esp_http_client_read(client, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                outBody.assign(buf, n);
            }
        }
        esp_http_client_cleanup(client);

        if (outStatus < 200 || outStatus >= 300) {
            errorMsg = "HTTP ";
            errorMsg += std::to_string(outStatus);
            return false;
        }
        return true;
    }

    // ── 解析脚本列表 ──
    static bool parseScriptList(const std::string& jsonText, std::vector<MarketScript>& out, std::string& errorMsg)
    {
        cJSON* root = cJSON_Parse(jsonText.c_str());
        if (!root) {
            errorMsg = "列表 JSON 解析失败";
            return false;
        }

        // 兼容纯数组 或 { "scripts": [...] } / { "data": [...] }
        cJSON* arr = root;
        if (!cJSON_IsArray(root)) {
            arr = cJSON_GetObjectItem(root, "scripts");
            if (!arr || !cJSON_IsArray(arr)) arr = cJSON_GetObjectItem(root, "data");
            if (!arr || !cJSON_IsArray(arr)) {
                errorMsg = "列表格式无效（期望数组或 {scripts:[...]}）";
                cJSON_Delete(root);
                return false;
            }
        }

        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, arr) {
            if (!cJSON_IsObject(item)) continue;
            MarketScript s;
            auto getStr = [&](const char* key, std::string& outStr) {
                cJSON* v = cJSON_GetObjectItem(item, key);
                if (v && cJSON_IsString(v) && v->valuestring)
                    outStr = v->valuestring;
            };
            auto getNum = [&](const char* key, double& outNum) {
                cJSON* v = cJSON_GetObjectItem(item, key);
                if (v && cJSON_IsNumber(v)) outNum = v->valuedouble;
            };
            getStr("id", s.id);
            getStr("name", s.name);
            getStr("author", s.author);
            getStr("version", s.version);
            getStr("updated", s.updated);
            getStr("desc", s.desc);
            getStr("thumb", s.thumb);
            getStr("screenshot", s.screenshot);
            getStr("download", s.download);
            double d = 0;
            getNum("size", d);    s.sizeBytes = (int)d;
            getNum("rating", d);  s.rating = (float)d;
            getNum("downloads", d); s.downloads = (int)d;
            if (s.name.empty()) s.name = "未命名";
            if (s.download.empty() && !s.id.empty())
                s.download = "/scripts/" + s.id + ".lua";
            out.push_back(std::move(s));
        }
        cJSON_Delete(root);
        return true;
    }

    // ── 各任务实现 ──
    static void executeFetchList(const ListCtx& ctx)
    {
        std::string url = joinUrl(ctx.baseUrl, "/api/scripts");
        std::string body, err;
        int status = 0;
        if (!httpGetBody(url, 10000, 2 * 1024 * 1024, body, status, err)) {
            if (ctx.cb) ctx.cb(false, {}, err);
            return;
        }
        std::vector<MarketScript> list;
        bool ok = parseScriptList(body, list, err);
        if (ctx.cb) ctx.cb(ok, list, err);
    }

    static void executeDownloadText(const TextCtx& ctx)
    {
        std::string url = joinUrl(ctx.baseUrl, ctx.relPath);
        std::string body, err;
        int status = 0;
        bool ok = httpGetBody(url, 15000, 512 * 1024, body, status, err);
        if (ctx.cb) ctx.cb(ok, body, err);
    }

    static void executeDownloadFile(const FileCtx& ctx)
    {
        std::string url = joinUrl(ctx.baseUrl, ctx.relPath);
        std::string body, err;
        int status = 0;
        if (!httpGetBody(url, 15000, 2 * 1024 * 1024, body, status, err)) {
            if (ctx.cb) ctx.cb(false, err);
            return;
        }
        FILE* f = fopen(ctx.destPath.c_str(), "wb");
        if (!f) {
            err = "无法写入文件: " + ctx.destPath;
            if (ctx.cb) ctx.cb(false, err);
            return;
        }
        size_t w = fwrite(body.data(), 1, body.size(), f);
        fclose(f);
        if (w != body.size()) {
            err = "文件写入不完整";
            if (ctx.cb) ctx.cb(false, err);
            return;
        }
        if (ctx.cb) ctx.cb(true, "");
    }

    static void executeReportDownload(const DownloadReportCtx& ctx)
    {
        std::string url = joinUrl(ctx.baseUrl, "/api/stats/download");
        std::string body = "{\"id\":\"" + ctx.id + "\"}";
        std::string resp, err;
        int status = 0;
        if (!httpPostJson(url, body, 5000, resp, status, err)) {
            ESP_LOGW(TAG, "上报下载失败(id=%s): %s", ctx.id.c_str(), err.c_str());
        }
    }
};
