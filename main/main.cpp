#include <esp_log.h>
#include <esp_event.h>

#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "task/task.hpp"

extern "C" void app_main(void)
{
	constexpr static char TAG[] = "main";
	ESP_LOGI(TAG, "started");

	Task::init(2);

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	nvsInit();

	wifiInit(true);
	wifiStart();
	wifiStationStart();

	wifi_ap_record_t wifis[20]{};
	wifiStationScan(wifis, sizeof(wifis));

	for (auto& i : wifis)
		ESP_LOGI(TAG, "RSSI: %d, SSID: %s", i.rssi, i.ssid);
}
