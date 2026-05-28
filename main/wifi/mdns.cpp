/**
 * @file mdns.cpp
 * @brief mDNS (Bonjour/Zeroconf) 服务封装
 *
 * 基于 ESP-IDF mdns 组件，提供主机名广播和服务发现功能。
 * 用户可通过 `esp32p4.local` 访问 HTTP/WS 服务器，无需记忆 IP 地址。
 *
 * 使用步骤:
 *   1. mdnsInit()          初始化 mdns
 *   2. mdnsStart()         设置主机名和实例名（如 "esp32p4"）
 *   3. mdnsServiceAdd()    添加服务（如 _http._tcp 端口 80）
 *
 * @note ESP-IDF mdns 无 stop/start API，只有 init/deinit
 */

#include "mdns.hpp"

#include <esp_log.h>
#include <esp_err.h>
#include <mdns.h>

static constexpr char TAG[] = "mdns";

static bool mdnsInited = false;

/**
 * @brief 初始化 mDNS
 *
 * 调用 mdns_init() 初始化 mDNS 服务。
 * 可多次调用，已初始化时直接返回 true。
 *
 * @return bool 初始化成功返回 true
 */
bool mdnsInit()
{
	if (mdnsInited)
	{
		ESP_LOGW(TAG, "inited already");
		return true;
	}

	ESP_LOGI(TAG, "init");

	esp_err_t err = mdns_init();
	if (err)
	{
		ESP_LOGE(TAG, "mdns init failed: %s", esp_err_to_name(err));
		return false;
	}

	mdnsInited = true;
	ESP_LOGI(TAG, "inited");
	return true;
}

/**
 * @brief 反初始化 mDNS
 *
 * 调用 mdns_free() 释放 mDNS 服务。
 * 注意：ESP-IDF mdns 无 stop 功能，反初始化后无法恢复，只能重新 mdnsInit()
 */
void mdnsDeinit()
{
	if (!mdnsInited)
	{
		ESP_LOGW(TAG, "not inited");
		return;
	}

	mdns_free();
	mdnsInited = false;
	ESP_LOGI(TAG, "deinited");
}

/**
 * @brief 设置 mDNS 主机名和实例名
 *
 * 在网络上广播主机名（如 esp32p4.local）和实例名。
 *
 * @param hostname     主机名（不含域名后缀），如 "esp32p4"
 * @param instanceName 实例名（友好名称），如 "ESP32P4 Game Console"，可传 NULL
 *
 * @return bool 设置成功返回 true
 */
bool mdnsStart(const char* hostname, const char* instanceName)
{
	if (!mdnsInited)
	{
		ESP_LOGE(TAG, "not inited");
		return false;
	}

	ESP_LOGI(TAG, "start hostname: %s, instance: %s", hostname, instanceName);

	esp_err_t err = mdns_hostname_set(hostname);
	if (err)
	{
		ESP_LOGE(TAG, "mdns hostname set failed: %s", esp_err_to_name(err));
		return false;
	}

	if (instanceName)
	{
		err = mdns_instance_name_set(instanceName);
		if (err)
		{
			ESP_LOGE(TAG, "mdns instance name set failed: %s", esp_err_to_name(err));
			return false;
		}
	}

	ESP_LOGI(TAG, "started");
	return true;
}

/**
 * @brief 添加 mDNS 服务
 *
 * 在网络上广播一个服务（如 HTTP 或 WebSocket），供 mDNS 客户端发现。
 *
 * @param instanceName  服务实例名（友好名称），如 "ESP32P4 HTTP"，可传 NULL 使用全局实例名
 * @param serviceType   服务类型，必须带下划线前缀，如 "_http", "_ws", "_ssh"
 * @param proto         协议，必须带下划线前缀，如 "_tcp", "_udp"
 * @param port          端口号
 *
 * @return bool 添加成功返回 true
 *
 * @note serviceType 和 proto 必须带下划线前缀，这是 mDNS DNS-SD 标准要求
 */
bool mdnsServiceAdd(const char* instanceName, const char* serviceType, const char* proto, uint16_t port)
{
	if (!mdnsInited)
	{
		ESP_LOGE(TAG, "not inited");
		return false;
	}

	auto err = mdns_service_add(instanceName, serviceType, proto, port, NULL, 0);
	if (err)
	{
		ESP_LOGE(TAG, "mdns service add failed: %s", esp_err_to_name(err));
		return false;
	}

	return true;
}
