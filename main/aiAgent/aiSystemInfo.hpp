#pragma once

#include <string>
#include <cstdio>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include "battery/batteryManager.hpp"
#include "wifi/wifi.hpp"
#include "storage/fat.hpp"
#include "bleGamepad/bleGamepad.hpp"

/**
 * @file aiSystemInfo.hpp
 * @brief 系统信息采集
 *
 * 收集设备各项状态数据，供 AI Agent 分析使用。
 * 独立头文件，AiExecutor 和 AiAgentOverlay 共用。
 */

namespace {

static constexpr char SYSINFO_TAG[] = "SystemInfo";

/**
 * @brief 采集完整的系统信息
 * @return 格式化后的多行字符串
 */
inline std::string collectSystemInfo() {
    std::string info;

    // ── 电池 ──
    int batPct = BatteryManager::instance().getPercent();
    if (batPct >= 0) {
        info += "- 电池电量: " + std::to_string(batPct) + "%";
        info += BatteryManager::instance().isCharging() ? " (充电中)" : "";
        info += "\n";
    }

    // ── WiFi ──
    if (wifiIsConnect()) {
        esp_ip4_addr_t ip = wifiStationGetIp();
        char ipStr[16];
        snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&ip));
        info += "- IP 地址: " + std::string(ipStr) + "\n";
        info += "- WiFi: 已连接\n";
    } else {
        info += "- WiFi: 未连接\n";
    }

    // AP 模式（AP 启动时）
    wifi_ap_config_t apCfg = wifiApGetInfo();
    if (apCfg.ssid[0] != '\0') {
        info += "- AP 模式 SSID: " + std::string((const char*)apCfg.ssid) + "\n";
    }

    // ── 存储 ──
    uint64_t freeBytes = 0, totalBytes = 0;
    getSpace(freeBytes, totalBytes, PerfixRoot);
    if (totalBytes > 0) {
        info += "- 存储空间: " + std::to_string(freeBytes / 1024) + "K / "
              + std::to_string(totalBytes / 1024) + "K 空闲\n";
    }

    // ── 内存 ──
    size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psramFree  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    info += "- PSRAM: " + std::to_string(psramFree / 1024) + "K / "
          + std::to_string(psramTotal / 1024) + "K 空闲\n";
    info += "- 内部 RAM 空闲: " + std::to_string(internalFree / 1024) + "K\n";

    // ── 运行时间 ──
    uint64_t uptimeSec = esp_timer_get_time() / 1000000;
    info += "- 运行时间: " + std::to_string(uptimeSec / 3600) + "h "
          + std::to_string((uptimeSec % 3600) / 60) + "m "
          + std::to_string(uptimeSec % 60) + "s\n";

    // ── 手柄 ──
    uint8_t padCount = BleGamepad::instance().connectedCount();
    info += "- 已连接手柄: " + std::to_string(padCount) + "\n";
    for (uint8_t p = 0; p < MaxPlayers; p++) {
        const auto* dev = BleGamepad::instance().getDevice(p);
        if (dev && dev->connected) {
            uint8_t bat = BleGamepad::instance().getBatteryLevel(p);
            info += "  - " + std::string(dev->name);
            if (bat <= 100)
                info += " (" + std::to_string(bat) + "%)";
            else
                info += " (电量未知)";
            info += "\n";
        }
    }

    // ── 固定信息 ──
    info += "- 屏幕分辨率: 720×1280\n";
    info += "- 型号: ESP32-P4\n";

    ESP_LOGI(SYSINFO_TAG, "采集完成:\n%s", info.c_str());
    return info;
}

} // anonymous namespace
