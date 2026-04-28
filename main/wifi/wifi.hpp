#include <esp_wifi_types_generic.h>
#include <esp_netif_ip_addr.h>

bool wifiIsInited();
void wifiInit(bool remote);
void wifiDeinit();

bool wifiIsStarted();
void wifiStart();
void wifiStop();

bool wifiStationIsStarted();
void wifiStationStart();
void wifiStationStop();
uint16_t wifiStationScan(wifi_ap_record_t* apInfo, uint16_t maxCount, char* ssid = nullptr);
esp_ip4_addr_t wifiStationGetIp();
wifi_sta_config_t wifiStationGetInfo();

bool wifiIsWantConnect();
bool wifiIsConnect();
void wifiConnect(const char* ssid, const char* password, unsigned char retryTime = 3);
void wifiDisconnect();

bool wifiApIsStarted();
void wifiApStart();
void wifiApSet(const char* ssid, const char* password, wifi_auth_mode_t authMode = wifi_auth_mode_t::WIFI_AUTH_WPA2_WPA3_PSK);
void wifiApStop();
wifi_ap_config_t wifiApGetInfo();

void wifiNatSetAutoStart(bool flag = true);
bool wifiNatIsAutoStart();

bool wifiNatIsStarted();
void wifiNatStart();
void wifiNatStop();
