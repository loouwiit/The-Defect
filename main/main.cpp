#include <esp_log.h>
#include <esp_event.h>

#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "task/task.hpp"

extern "C" void app_main(void)
{
	constexpr static char TAG[] = "main";

	Task::init(2);

	ESP_LOGI(TAG, "esp_event_loop_create_default");
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	ESP_LOGI(TAG, "nvsInit");
	nvsInit();

	ESP_LOGI(TAG, "wifiInit");
	wifiInit(true);

	ESP_LOGI(TAG, "wifiStart");
	wifiStart();

	ESP_LOGI(TAG, "wifiStationStart");
	wifiStationStart();

	ESP_LOGI(TAG, "wifiStationScan");
	constexpr static size_t wifiSize = 20;
	wifi_ap_record_t* wifis = new wifi_ap_record_t[wifiSize];
	wifiStationScan(wifis, wifiSize);

	for (int i = 0; i < wifiSize; i++)
		ESP_LOGI(TAG, "RSSI: %d, SSID: %s", wifis[i].rssi, wifis[i].ssid);

	delete[] wifis;
}
