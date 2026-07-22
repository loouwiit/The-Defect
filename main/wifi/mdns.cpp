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
 * 主机名自定义：
 *   - 通过 mdnsSetHostname() 持久化到 NVS 并立即热重启 mDNS
 *   - 服务实例名自动跟随主机名更新
 *
 * @note ESP-IDF mdns 无 stop/start API，只有 init/deinit
 */

#include "mdns.hpp"

#include <esp_log.h>
#include <esp_err.h>
#include <mdns.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>
#include <cctype>

static constexpr char TAG[] = "mdns";

// ── NVS ──
static constexpr char NvsNamespace[] = "mdns";
static constexpr char NvsKeyHostname[] = "hostname";

// ── 内部状态 ──
static bool mdnsInited = false;
static char s_currentHostname[64] = "";   // 当前活跃的 hostname
static constexpr char DefaultHostname[] = "esp32p4";

// ── 服务注册表（用于 mDNS 热重启时重新注册） ──
struct ServiceRecord {
	char suffix[64];  // 实例名中 hostname 之后的部分，如 " HTTP"
	char type[16];    // "_http"
	char proto[8];    // "_tcp"
	uint16_t port;
};

static constexpr int MaxServices = 8;
static ServiceRecord s_services[MaxServices];
static int s_serviceCount = 0;

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
 * @brief 内部：重新注册所有服务（mDNS 热重启时调用）
 *
 * 遍历注册表，用当前 hostname 重新生成实例名后注册到 ESP-IDF mdns。
 */
static void mdns_restart_services()
{
	char instanceName[128];

	for (int i = 0; i < s_serviceCount; i++)
	{
		instanceName[0] = '\0';
		if (s_services[i].suffix[0])
			snprintf(instanceName, sizeof(instanceName), "%s%s",
				s_currentHostname, s_services[i].suffix);

		auto err = mdns_service_add(
			instanceName[0] ? instanceName : NULL,
			s_services[i].type,
			s_services[i].proto,
			s_services[i].port,
			NULL, 0);

		if (err)
			ESP_LOGE(TAG, "re-add service %s/%s failed: %s",
				s_services[i].type, s_services[i].proto, esp_err_to_name(err));
		else
			ESP_LOGI(TAG, "re-added service %s/%s on port %u",
				s_services[i].type, s_services[i].proto, s_services[i].port);
	}
}

/**
 * @brief 设置 mDNS 主机名和实例名
 *
 * 在网络上广播主机名（如 esp32p4.local）和实例名。
 * 同时更新内部 hostname 缓存。
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

	// 缓存当前 hostname
	if (hostname)
		strncpy(s_currentHostname, hostname, sizeof(s_currentHostname) - 1);

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
 * 同时将服务记录存入注册表，供热重启时重新注册。
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

	// ── 存入注册表（用于热重启时重新广播） ──
	if (s_serviceCount < MaxServices)
	{
		auto& rec = s_services[s_serviceCount];

		// 提取实例名中 hostname 之后的部分作为 suffix
		// 如 instanceName="esp32p4 HTTP" → suffix=" HTTP"
		rec.suffix[0] = '\0';
		if (instanceName)
		{
			size_t prefixLen = strlen(s_currentHostname);
			const char* suffix = instanceName;
			if (prefixLen > 0 && strncmp(instanceName, s_currentHostname, prefixLen) == 0)
				suffix = instanceName + prefixLen;
			strncpy(rec.suffix, suffix, sizeof(rec.suffix) - 1);
		}

		strncpy(rec.type, serviceType, sizeof(rec.type) - 1);
		strncpy(rec.proto, proto, sizeof(rec.proto) - 1);
		rec.port = port;
		s_serviceCount++;
	}
	else
	{
		ESP_LOGW(TAG, "service registry full (%d), not stored", MaxServices);
	}

	// ── 注册到 ESP-IDF mdns ──
	auto err = mdns_service_add(instanceName, serviceType, proto, port, NULL, 0);
	if (err)
	{
		ESP_LOGE(TAG, "mdns service add failed: %s", esp_err_to_name(err));
		return false;
	}

	return true;
}

// ════════════════════════════════════════════════════════════════
// 主机名查询 / 修改
// ════════════════════════════════════════════════════════════════

const char* mdnsGetHostname()
{
	if (s_currentHostname[0] == '\0')
		return mdnsGetConfiguredHostname();
	return s_currentHostname;
}

const char* mdnsGetConfiguredHostname()
{
	// 如果 s_currentHostname 非空且不为默认值，说明已被 mdnsSetHostname() 设置过
	// → 直接返回 s_currentHostname（它已经是最新的）
	if (s_currentHostname[0] != '\0')
		return s_currentHostname;

	// 从 NVS 读取
	static char hostname[64] = "";

	if (hostname[0] == '\0')
	{
		nvs_handle handle;
		esp_err_t err = nvs_open(NvsNamespace, NVS_READONLY, &handle);
		if (err == ESP_OK)
		{
			size_t len = sizeof(hostname);
			err = nvs_get_str(handle, NvsKeyHostname, hostname, &len);
			nvs_close(handle);
		}

		if (hostname[0] == '\0')
			strncpy(hostname, DefaultHostname, sizeof(hostname) - 1);
	}

	return hostname;
}

bool mdnsSetHostname(const char* hostname)
{
	if (!hostname || hostname[0] == '\0')
	{
		ESP_LOGE(TAG, "empty hostname");
		return false;
	}

	if (strlen(hostname) > 48)
	{
		ESP_LOGE(TAG, "hostname too long (max 48 chars)");
		return false;
	}

	// 验证主机名：仅允许字母、数字、连字符
	for (const char* p = hostname; *p; p++)
	{
		if (!isalnum((unsigned char)*p) && *p != '-')
		{
			ESP_LOGE(TAG, "invalid hostname char: '%c' (only a-z, 0-9, - allowed)", *p);
			return false;
		}
	}

	ESP_LOGI(TAG, "set hostname: %s", hostname);

	// ── 持久化到 NVS ──
	nvs_handle handle;
	esp_err_t err = nvs_open(NvsNamespace, NVS_READWRITE, &handle);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "NVS open failed: %d", err);
		return false;
	}

	err = nvs_set_str(handle, NvsKeyHostname, hostname);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "NVS set failed: %d", err);
		nvs_close(handle);
		return false;
	}
	nvs_commit(handle);
	nvs_close(handle);

	// ── 更新缓存 ──
	strncpy(s_currentHostname, hostname, sizeof(s_currentHostname) - 1);

	// ── 清除 NVS 静态缓存，使下次 mdnsGetConfiguredHostname() 读到新值 ──
	{
		// 通过强制让静态缓存失效来实现：写入一个特殊标记
		// 简单做法：将静态缓存的首字节置为 '\0'，下次调用会重新读 NVS
		// 但由于 mdnsGetConfiguredHostname 用的是 static char hostname[64]，
		// 这里无法直接访问它。我们通过 mdnsGetConfiguredHostname 的机制间接处理：
		// 它实际上是懒加载的，main.cpp 在启动时已经调用了 mdnsGetConfiguredHostname()，
		// 所以缓存已填充。但由于 mdnsSetHostname 可能被多次调用，需要一种方式刷新。
		// 方案：mdnsGetConfiguredHostname 读 NVS 后缓存，但 NVS 值在本次已更新，
		// 而静态缓存不会自动刷新。所以我们直接修改 s_currentHostname 为主接口。
	}
	// 注意：mdnsGetConfiguredHostname() 内部有 static 缓存，只在首次调用时读 NVS。
	// 此处刚更新了 NVS，但 static 缓存是旧值。
	// 没关系——mdnsGetHostname() 优先返回 s_currentHostname（已更新），
	// mdnsGetConfiguredHostname() 用于启动时读取，运行中不再使用。

	// ── mDNS 热重启 ──
	if (mdnsInited)
	{
		char instanceName[128];
		snprintf(instanceName, sizeof(instanceName), "%s Game Console", s_currentHostname);

		ESP_LOGI(TAG, "mdns hot restart with hostname: %s", s_currentHostname);

		mdnsDeinit();

		if (!mdnsInit())
		{
			ESP_LOGE(TAG, "mdns reinit failed after hostname change");
			return false;
		}

		if (!mdnsStart(s_currentHostname, instanceName))
		{
			ESP_LOGE(TAG, "mdns restart failed after hostname change");
			return false;
		}

		mdns_restart_services();

		ESP_LOGI(TAG, "mdns hot restart done");
	}
	else
	{
		ESP_LOGI(TAG, "mdns not active, hostname saved to NVS for next start");
	}

	return true;
}
