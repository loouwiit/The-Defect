#pragma once

#include <cstdint>

/**
 * @brief 发现的游戏主机信息
 */
struct DiscoveredConsole {
	char hostname[64];  // mDNS 主机名，如 "esp32p4-2"
	char ip[16];        // IPv4 地址字符串，如 "192.168.1.42"
	uint16_t port;      // 服务端口（_p4console 注册的端口）
};

/**
 * @brief 发现局域网内其他游戏主机
 *
 * 查询 _p4console._tcp 服务，返回所有匹配设备（排除自身）。
 * 同步阻塞，适合在 Task 或后台线程中调用。
 *
 * @param consoles  输出数组，接收发现的设备
 * @param maxCount  数组容量
 * @param timeoutMs 查询超时（毫秒），默认 3000
 * @return int      实际发现的设备数量
 */
int discoverConsoles(DiscoveredConsole* consoles, int maxCount,
                     uint32_t timeoutMs = 3000);
