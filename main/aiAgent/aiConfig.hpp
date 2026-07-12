#pragma once

#include <string>
#include <cstdio>
#include <cJSON.h>
#include <esp_log.h>
#include <nvs_flash.h>

/**
 * @file aiConfig.hpp
 * @brief AI Agent 配置加载
 *
 * 从 FAT 分区配置文件 + NVS 加载 AI 连接配置。
 *
 * 配置文件路径: /root/system/ai_config.json (FAT 分区)
 * NVS 键: ai_api_key (string)
 */

struct AiConfig {
    std::string apiUrl = "https://api.openai.com";  // API 基地址
    std::string model  = "gpt-4o-mini";             // 模型名
    std::string apiKey;                              // API Key（从 NVS 读取）
    float temperature = 0.7f;                        // 温度参数
    int maxTokens     = 1024;                        // 最大 token 数
    int timeoutMs     = 30000;                       // HTTP 超时（毫秒）

    bool valid = false;                              // 配置是否有效（有 API Key）
};

namespace {

constexpr char TAG[] = "AiConfig";
constexpr char CONFIG_PATH[] = "/root/system/ai_config.json";
constexpr char NVS_KEY[] = "ai_api_key";
constexpr char NVS_NAMESPACE[] = "ai_agent";

/// 从 FAT 分区 JSON 文件加载配置
static bool loadConfigFromFile(AiConfig& cfg) {
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "配置文件 %s 不存在，使用默认值", CONFIG_PATH);
        return false;
    }

    // 读取文件内容
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    char* buf = new char[len + 1];
    size_t readLen = fread(buf, 1, len, f);
    buf[readLen] = '\0';
    fclose(f);

    // 解析 JSON
    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "配置文件 JSON 解析失败");
        delete[] buf;
        return false;
    }

    // 读取各字段
    auto readStr = [&](const char* key, std::string& out) {
        cJSON* item = cJSON_GetObjectItem(root, key);
        if (item && cJSON_IsString(item) && item->valuestring[0] != '\0')
            out = item->valuestring;
    };
    auto readInt = [&](const char* key, int& out) {
        cJSON* item = cJSON_GetObjectItem(root, key);
        if (item && cJSON_IsNumber(item))
            out = item->valueint;
    };
    auto readFloat = [&](const char* key, float& out) {
        cJSON* item = cJSON_GetObjectItem(root, key);
        if (item && cJSON_IsNumber(item))
            out = static_cast<float>(item->valuedouble);
    };

    readStr("api_url",     cfg.apiUrl);
    readStr("model",       cfg.model);
    readFloat("temperature", cfg.temperature);
    readInt("max_tokens",  cfg.maxTokens);
    readInt("timeout_ms",  cfg.timeoutMs);

    cJSON_Delete(root);
    delete[] buf;
    return true;
}

/// 从 NVS 读取 API Key
static bool loadApiKeyFromNvs(AiConfig& cfg) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 命名空间 %s 不存在", NVS_NAMESPACE);
        return false;
    }

    size_t len = 0;
    err = nvs_get_str(handle, NVS_KEY, nullptr, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(handle);
        return false;
    }

    cfg.apiKey.resize(len);
    nvs_get_str(handle, NVS_KEY, cfg.apiKey.data(), &len);
    nvs_close(handle);

    // 去掉末尾多余的 null
    while (!cfg.apiKey.empty() && cfg.apiKey.back() == '\0')
        cfg.apiKey.pop_back();

    return !cfg.apiKey.empty();
}

} // anonymous namespace

/// 加载完整配置（FAT 文件 + NVS）
/// @return AiConfig，valid=false 表示配置不完整
inline AiConfig aiConfigLoad() {
    AiConfig cfg;
    loadConfigFromFile(cfg);
    loadApiKeyFromNvs(cfg);
    cfg.valid = !cfg.apiKey.empty();
    return cfg;
}

/// 保存 API Key 到 NVS
inline bool aiConfigSaveApiKey(const std::string& apiKey) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_str(handle, NVS_KEY, apiKey.c_str());
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
