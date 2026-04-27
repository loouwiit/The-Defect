#include <cstring>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_wifi_remote.h>

#include "lwip/lwip_napt.h"

#include "dhcpserver/dhcpserver.h"

#include "wifi.hpp"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

const uint32_t defaultNatIp = esp_ip4addr_aton("192.168.4.1");

constexpr char TAG[] = "wifi";

EXT_RAM_BSS_ATTR unsigned char wifiRetryCount = 0;

EXT_RAM_BSS_ATTR bool wifiInited = false;
EXT_RAM_BSS_ATTR bool wifiStarted = false;
EXT_RAM_BSS_ATTR bool wifiStationStarted = false;
EXT_RAM_BSS_ATTR bool wifiStationWantConnect = false;
EXT_RAM_BSS_ATTR bool wifiStationConnected = false;
EXT_RAM_BSS_ATTR bool wifiApStarted = false;

EXT_RAM_BSS_ATTR bool wifiNatAutoStart = false;
EXT_RAM_BSS_ATTR bool wifiNatStarted = false;

EXT_RAM_BSS_ATTR esp_netif_t* wifiAp = nullptr;
EXT_RAM_BSS_ATTR esp_netif_t* wifiSta = nullptr;

EXT_RAM_BSS_ATTR esp_ip4_addr_t ip = { 0 };

bool wifiStationSetConfig(const char* ssid, const char* password);
bool wifiNatAutoStartDetecte();

void event_handler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
	ESP_LOGD(TAG, "event base = %s, id = %ld", eventBase, eventId);

	if (eventBase == WIFI_EVENT)
	{
		//AP
		if (eventId == WIFI_EVENT_AP_STACONNECTED)
		{
			wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)eventData;
			ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
				MAC2STR(event->mac), event->aid);
			return;
		}

		if (eventId == WIFI_EVENT_AP_STADISCONNECTED)
		{
			wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)eventData;
			ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
				MAC2STR(event->mac), event->aid, event->reason);
			return;
		}

		//STA
		if (eventId != WIFI_EVENT_STA_DISCONNECTED) return;

		if (!wifiStationWantConnect)
		{
			// 不想连接
			ESP_LOGI(TAG, "disconnect success");
			ip = { 0 };
			wifiStationConnected = false;
			return;
		}

		// 想连接

		// 判断意外断连
		if (wifiStationConnected)
		{
			// 之前连接 -> 现在丢失连接
			ESP_LOGI(TAG, "connection lost");
			ip = { 0 };
			wifiStationConnected = false;
		}

		// 尝试重连
		if (wifiRetryCount > 0)
		{
			ESP_LOGI(TAG, "retry to connect");
			wifiRetryCount--;
			esp_wifi_connect();
		}
		else
		{
			// 彻底失败
			ESP_LOGI(TAG, "failed to connect");
			wifiStationWantConnect = false; //停止后续继续尝试
		}
	}
	else if (eventBase == IP_EVENT)
	{
		if (eventId == IP_EVENT_STA_GOT_IP)
		{
			ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
			ESP_LOGI(TAG, "connect success");
			ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
			ip = event->ip_info.ip;
			wifiStationConnected = true;
			wifiNatAutoStartDetecte();
		}
	}
}

bool wifiIsInited()
{
	return wifiInited;
}

void wifiInit(bool remote)
{
	if (wifiInited)
	{
		ESP_LOGW(TAG, "inited already");
		return;
	}
	wifiInited = true;

	ESP_LOGI(TAG, "init");

	ESP_ERROR_CHECK(esp_netif_init());

	wifiSta = esp_netif_create_default_wifi_sta();
	wifiAp = esp_netif_create_default_wifi_ap();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	if (remote)
		ESP_ERROR_CHECK(esp_wifi_remote_init(&cfg));
	else
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	// 注册消息
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, nullptr, nullptr));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr, nullptr));

	ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_NULL));

	ESP_LOGI(TAG, "inited");
}

void wifiDeinit()
{
	if (!wifiInited)
	{
		ESP_LOGW(TAG, "not inited, no need to deinit");
		return;
	}
	wifiInited = false;

	esp_wifi_deinit();
	esp_netif_destroy_default_wifi(wifiSta);
	wifiSta = nullptr;
	esp_netif_destroy_default_wifi(wifiAp);
	wifiAp = nullptr;
	esp_netif_deinit();
}

bool wifiStationSetConfig(const char* ssid, const char* password)
{
	ESP_LOGI(TAG, "searching for %s", ssid);

	wifi_scan_config_t scan;
	memset(&scan, 0, sizeof(scan));
	scan.ssid = (uint8_t*)ssid;
	esp_wifi_scan_start(&scan, true);

	wifi_ap_record_t apInfo;
	memset(&apInfo, 0, sizeof(wifi_ap_record_t));

	if (esp_wifi_scan_get_ap_record(&apInfo) != ESP_OK)
	{
		ESP_LOGI(TAG, "not searched");
		return false;
	}


	ESP_LOGI(TAG, "setting config");

	wifi_config_t config;
	memset(&config, 0, sizeof(config));

	sprintf((char*)config.sta.ssid, "%s", ssid);
	sprintf((char*)config.sta.password, "%s", password);
	memcpy(config.sta.bssid, apInfo.bssid, sizeof(apInfo.bssid));
	config.sta.channel = apInfo.primary;
	config.sta.scan_method = wifi_scan_method_t::WIFI_FAST_SCAN;
	config.sta.sort_method = wifi_sort_method_t::WIFI_CONNECT_AP_BY_SIGNAL;
	config.sta.threshold.authmode = apInfo.authmode;


	esp_wifi_set_config(WIFI_IF_STA, &config);
	ESP_LOGI(TAG, "config setted");

	return true;
}

bool wifiIsStarted()
{
	return wifiStarted;
}

void wifiStart()
{
	if (wifiStarted)
	{
		ESP_LOGW(TAG, "has started, don't need start");
		return;
	}
	esp_wifi_start();
	wifiStarted = true;
	wifiStationWantConnect = false;
	ip = { 0 };
	wifiStationConnected = false;
	wifiNatStarted = false;
	ESP_LOGI(TAG, "started");
}

void wifiStop()
{
	if (!wifiStarted)
	{
		ESP_LOGW(TAG, "hasn't started, don't need stop");
		return;
	}
	if (wifiApStarted) wifiApStop();
	if (wifiStationStarted) wifiStationStop();
	if (wifiNatStarted) wifiNatStop();
	esp_wifi_stop();
	wifiStarted = false;
	wifiStationStarted = false;
	wifiStationWantConnect = false;
	ip = { 0 };
	wifiStationConnected = false;
	wifiApStarted = false;
	wifiNatStarted = false;
	ESP_LOGI(TAG, "stopped");
}

bool wifiStationIsStarted()
{
	return wifiStationStarted;
}

void wifiStationStart()
{
	if (wifiStationStarted)
	{
		ESP_LOGW(TAG, "station has started, don't need start");
		return;
	}
	wifiStationStarted = true;
	wifiStationWantConnect = false;
	ip = { 0 };
	wifiStationConnected = false;
	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);
	switch (mode)
	{
	case wifi_mode_t::WIFI_MODE_STA:
	case wifi_mode_t::WIFI_MODE_APSTA:
		break;
	case wifi_mode_t::WIFI_MODE_AP:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_APSTA);
		break;
	default:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_STA);
		break;
	}
	ESP_LOGI(TAG, "station started");
}

void wifiStationStop()
{
	if (!wifiStationStarted)
	{
		ESP_LOGW(TAG, "station hasn't started, don't need stop");
		return;
	}
	if (wifiStationWantConnect) wifiDisconnect();
	wifiStationStarted = false;
	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);
	switch (mode)
	{
	case wifi_mode_t::WIFI_MODE_APSTA:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_AP);
		break;
	case wifi_mode_t::WIFI_MODE_AP:
		break;
	default:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_NULL);
		break;
	}
	ESP_LOGI(TAG, "station stopped");
}

uint16_t wifiStationScan(wifi_ap_record_t* apInfo, uint16_t maxCount, char* ssid)
{
	if (!wifiStationStarted)
	{
		ESP_LOGE(TAG, "station hasn't started, can't scan");
		return 0;
	}

	wifi_scan_config_t scan;
	memset(&scan, 0, sizeof(scan));
	scan.ssid = (uint8_t*)ssid;
	esp_wifi_scan_start(&scan, true);

	memset(apInfo, 0, sizeof(wifi_ap_record_t) * maxCount);

	uint16_t count = 0;
	esp_wifi_scan_get_ap_num(&count);
	esp_wifi_scan_get_ap_records(&maxCount, apInfo);
	ESP_LOGI(TAG, "station scaned %u Ap", (unsigned)count);
	return count;
}

esp_ip4_addr_t wifiStationGetIp()
{
	return ip;
}

wifi_sta_config_t wifiStationGetInfo()
{
	wifi_config_t wifi{};
	auto ret = esp_wifi_get_config(WIFI_IF_STA, &wifi);
	ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
	return wifi.sta;
}

bool wifiIsWantConnect()
{
	return wifiStationWantConnect;
}

bool wifiIsConnect()
{
	// 理论上 aid == 0 意味着未连接
	// 但是返回一直有值，于是自己写一个bool得了
	// uint16_t aid;
	// esp_wifi_sta_get_aid(&aid);
	// return aid != 0;
	return wifiStationConnected;
}

void wifiConnect(const char* ssid, const char* password, unsigned char retryTime)
{
	if (!wifiStationStarted)
	{
		ESP_LOGE(TAG, "station hasn't started, can't connet");
		return;
	}

	// config
	if (!wifiStationSetConfig(ssid, password))
	{
		// 设置失败
		ESP_LOGI(TAG, "connect failed");
		return;
	}

	if (wifiStationWantConnect) wifiDisconnect();

	wifiStationWantConnect = true;
	wifiRetryCount = retryTime;
	esp_wifi_connect();
}

void wifiDisconnect()
{
	if (!wifiStationStarted)
	{
		ESP_LOGE(TAG, "station hasn't started, can't disconnet");
		return;
	}
	if (!wifiStationWantConnect)
	{
		ESP_LOGW(TAG, "don't want to connect, don't need disconnect");
		return;
	}
	wifiStationWantConnect = false;
	ip = { 0 };
	wifiStationConnected = false; //貌似是冗余
	wifiNatStarted = false;
	esp_wifi_disconnect();
}

bool wifiApIsStarted()
{
	return wifiApStarted;
}

void wifiApStart()
{
	if (wifiApStarted)
	{
		ESP_LOGW(TAG, "Ap has started, don't need start");
		return;
	}
	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);
	switch (mode)
	{
	case wifi_mode_t::WIFI_MODE_AP:
	case wifi_mode_t::WIFI_MODE_APSTA:
		break;
	case wifi_mode_t::WIFI_MODE_STA:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_APSTA);
		break;
	default:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_AP);
		break;
	}
	wifiApStarted = true;
	wifiNatAutoStartDetecte();

	ESP_LOGI(TAG, "Ap started");
}

void wifiApSet(const char* ssid, const char* password, wifi_auth_mode_t authMode)
{
	wifi_config_t config;
	memset(&config, 0, sizeof(config));

	strcpy((char*)config.ap.ssid, ssid);
	// config.ap.channel = 0;
	strcpy((char*)config.ap.password, password);
	config.ap.max_connection = 4;
	config.ap.authmode = authMode;
	config.ap.pmf_cfg.required = true;

	esp_wifi_set_config(WIFI_IF_AP, &config);


	// DNS
	// Enable DNS (offer) for dhcp server
	esp_netif_dns_info_t dnsserver;
	dhcps_offer_t dhcps_dns_value = OFFER_DNS;
	esp_netif_dhcps_option(wifiAp, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_dns_value, sizeof(dhcps_dns_value));

	// // Set custom dns server address for dhcp server
	dnsserver.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");
	dnsserver.ip.type = ESP_IPADDR_TYPE_V4;
	esp_netif_set_dns_info(wifiAp, ESP_NETIF_DNS_MAIN, &dnsserver);
}

void wifiApStop()
{
	if (!wifiApStarted)
	{
		ESP_LOGW(TAG, "Ap hasn't started, don't need stop");
		return;
	}
	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);
	switch (mode)
	{
	case wifi_mode_t::WIFI_MODE_APSTA:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_STA);
		break;
	case wifi_mode_t::WIFI_MODE_STA:
		break;
	default:
		esp_wifi_set_mode(wifi_mode_t::WIFI_MODE_NULL);
		break;
	}
	wifiApStarted = false;
	wifiNatStarted = false;
	ESP_LOGI(TAG, "Ap stopped");
}

wifi_ap_config_t wifiApGetInfo()
{
	wifi_config_t wifi{};
	auto ret = esp_wifi_get_config(WIFI_IF_AP, &wifi);
	ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
	return wifi.ap;
}

void wifiNatSetAutoStart(bool flag)
{
	wifiNatAutoStart = flag;
}

bool wifiNatIsAutoStart()
{
	return wifiNatAutoStart;
}

bool wifiNatIsStarted()
{
	return wifiNatStarted;
}

void wifiNatStart()
{
	if (wifiNatStarted)
	{
		ESP_LOGW(TAG, "nat has started, don't need start");
		return;
	}
	if (!wifiStationConnected)
	{
		ESP_LOGW(TAG, "wifi don't connected, can't start nat");
		return;
	}

	ESP_LOGI(TAG, "enable nat");
	ip_napt_enable(defaultNatIp, 1);
	wifiNatStarted = true;
}

void wifiNatStop()
{
	if (wifiNatStarted)
	{
		ESP_LOGW(TAG, "nat has started, don't need stop");
		return;
	}

	ESP_LOGI(TAG, "disable nat");
	ip_napt_enable(defaultNatIp, 0);
	wifiNatStarted = false;
}

bool wifiNatAutoStartDetecte()
{
	if (wifiNatAutoStart && wifiStationConnected && wifiApStarted && !wifiNatStarted)
	{
		ESP_LOGI(TAG, "nat auto start");
		wifiNatStart();
		return true;
	}

	return false;
}
