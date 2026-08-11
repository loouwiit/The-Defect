#pragma once

#include <string>
#include <cstdio>
#include <cJSON.h>
#include <esp_log.h>
#include "storage/fat.hpp"

/**
 * @file marketConfig.hpp
 * @brief 游戏市场服务器配置
 *
 * 从 FAT 分区配置文件加载 / 保存市场服务器地址。
 * 配置文件路径: /root/system/market_config.json (FAT 分区)
 *
 * 示例：
 * {
 *   "server_url": "http://192.168.1.100:3000",
 *   "timeout_ms": 10000
 * }
 */

struct MarketConfig {
    std::string serverUrl;   // 市场服务器基地址，如 http://192.168.1.100:3000
    int timeoutMs = 10000;   // HTTP 超时（毫秒）
    bool valid = false;      // 配置是否有效（有 URL）
};

namespace {

static constexpr char CFG_TAG[] = "MarketConfig";
constexpr char CONFIG_PATH[] = "/root/system/market_config.json";

static bool loadConfigFromFile(MarketConfig& cfg)
{
    FILE* f = fopen(CONFIG_PATH, "r");
    if (!f) {
        ESP_LOGW(CFG_TAG, "配置文件 %s 不存在，使用默认值", CONFIG_PATH);
        return false;
    }

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

    cJSON* root = cJSON_Parse(buf);
    delete[] buf;
    if (!root) {
        ESP_LOGE(CFG_TAG, "配置文件 JSON 解析失败");
        return false;
    }

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

    readStr("server_url", cfg.serverUrl);
    readInt("timeout_ms", cfg.timeoutMs);

    cJSON_Delete(root);
    return true;
}

} // anonymous namespace

/// 加载市场配置
inline MarketConfig marketConfigLoad()
{
    MarketConfig cfg;
    loadConfigFromFile(cfg);
    cfg.valid = !cfg.serverUrl.empty();
    return cfg;
}

/// 获取市场服务器基地址（去末尾斜杠）
inline std::string marketConfigGetUrl()
{
    MarketConfig cfg = marketConfigLoad();
    std::string url = cfg.serverUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

/// 保存市场服务器地址到 FAT 配置（保留原有 timeout_ms）
/// @return true 保存成功；false 失败（目录创建 / 文件写入错误）
inline bool marketConfigSaveUrl(const std::string& url)
{
    // 读现有配置，保留 timeout_ms
    MarketConfig cfg;
    loadConfigFromFile(cfg);

    // 构造 JSON
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "server_url", url.c_str());
    cJSON_AddNumberToObject(root, "timeout_ms", cfg.timeoutMs);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return false;

    // 确保目录存在（FAT 无自动建目录）
    // ⚠ newFloor 直接 mkdir()：目录**已存在**时返回 EEXIST=false，
    //   误报"无法创建"。须先 testFloor 判断，仅缺失时再 mkdir
    if (!testFloor("/root/system/") && !newFloor("/root/system/")) {
        ESP_LOGE(CFG_TAG, "无法创建目录 /root/system/");
        free(json);
        return false;
    }

    bool ok = false;
    {
        // ⚠ OFile::open 为 O_RDWR|O_CREAT（无 O_TRUNC），URL 变短会残留旧字节
        //   → 先删后写，确保覆写完整
        removeFile(CONFIG_PATH);
        OFile file;
        if (file.open(CONFIG_PATH)) {
            size_t len = strlen(json);
            ok = (file.write(json, len) == len);
            file.close();
            if (ok)
                ESP_LOGI(CFG_TAG, "已保存市场服务器地址: %s", url.c_str());
            else
                ESP_LOGE(CFG_TAG, "写入 %s 不完整", CONFIG_PATH);
        }
        else {
            ESP_LOGE(CFG_TAG, "无法打开 %s 写入", CONFIG_PATH);
        }
    }
    free(json);
    return ok;
}
