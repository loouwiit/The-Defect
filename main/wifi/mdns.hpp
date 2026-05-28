#pragma once

#include <stdint.h>

/**
 * @brief 初始化 mDNS
 * @return bool 初始化成功返回 true
 */
bool mdnsInit();

/**
 * @brief 反初始化 mDNS
 *
 * @note ESP-IDF mdns 无 stop 功能，反初始化后无法恢复，只能重新 mdnsInit()
 */
void mdnsDeinit();

/**
 * @brief 设置 mDNS 主机名和实例名
 * @param hostname     主机名（不含域名后缀），如 "esp32p4"
 * @param instanceName 实例名（友好名称），如 "ESP32P4 Game Console"，可传 NULL
 * @return bool 设置成功返回 true
 */
bool mdnsStart(const char* hostname, const char* instanceName);

/**
 * @brief 添加 mDNS 服务
 *
 * @param instanceName  服务实例名（友好名称），如 "ESP32P4 HTTP"，可传 NULL 使用全局实例名
 * @param serviceType   服务类型，必须带下划线前缀，如 "_http", "_ws"
 * @param proto         协议，必须带下划线前缀，如 "_tcp", "_udp"
 * @param port          端口号
 * @return bool 添加成功返回 true
 *
 * @note serviceType 和 proto 必须带下划线前缀
 */
bool mdnsServiceAdd(const char* instanceName, const char* serviceType, const char* proto, uint16_t port);
