/**
 * @file discovery.cpp
 * @brief mDNS 设备发现 — 查询 _p4console._tcp 服务
 *
 * 通过 ESP-IDF mdns 组件同步查询局域网内其他游戏主机，
 * 返回主机名、IP、端口，过滤掉自身。
 */

#include "discovery.hpp"
#include "wifi/mdns.hpp"

#include <esp_log.h>
#include <mdns.h>
#include <cstring>

static constexpr char TAG[] = "discovery";

int discoverConsoles(DiscoveredConsole* consoles, int maxCount,
                     uint32_t timeoutMs)
{
	if (!consoles || maxCount <= 0)
	{
		ESP_LOGW(TAG, "invalid params");
		return 0;
	}

	// ── 查询 _p4console._tcp 服务 ──
	mdns_result_t* results = nullptr;
	esp_err_t err = mdns_query_ptr("_p4console", "_tcp",
		timeoutMs, maxCount, &results);

	if (err)
	{
		ESP_LOGE(TAG, "mdns query failed: %s", esp_err_to_name(err));
		return 0;
	}

	if (!results)
	{
		ESP_LOGI(TAG, "no consoles found");
		return 0;
	}

	// ── 遍历结果 ──
	int count = 0;
	const char* selfHostname = mdnsGetHostname();

	for (mdns_result_t* r = results; r && count < maxCount; r = r->next)
	{
		// 跳过自身
		if (r->hostname && selfHostname &&
			strcmp(r->hostname, selfHostname) == 0)
		{
			ESP_LOGD(TAG, "skip self: %s", r->hostname);
			continue;
		}

		auto& c = consoles[count];

		// 主机名
		c.hostname[0] = '\0';
		if (r->hostname)
			strncpy(c.hostname, r->hostname, sizeof(c.hostname) - 1);

		// 端口
		c.port = r->port;

		// IPv4 地址
		c.ip[0] = '\0';
		for (mdns_ip_addr_t* a = r->addr; a; a = a->next)
		{
			if (a->addr.type == ESP_IPADDR_TYPE_V4)
			{
				snprintf(c.ip, sizeof(c.ip),
					IPSTR, IP2STR(&a->addr.u_addr.ip4));
				break;
			}
		}

		if (c.ip[0] == '\0')
		{
			ESP_LOGW(TAG, "skip %s: no IPv4 address", c.hostname);
			continue;
		}

		ESP_LOGI(TAG, "found: %s @ %s:%u", c.hostname, c.ip, c.port);
		count++;
	}

	mdns_query_results_free(results);
	return count;
}
