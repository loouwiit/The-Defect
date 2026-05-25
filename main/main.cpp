#include "display/display.hpp"
#include "touch/touch.hpp"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_event.h>
#include <esp_err.h>


#include "task/task.hpp"
#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "server/serverKernal.hpp"

#include "storage/fat.hpp"
#include "storage/mem.hpp"
#include "storage/sd.hpp"

static constexpr char TAG[] = "main";

extern "C" void app_main(void)
{
	Task::init(2);

	ESP_LOGI(TAG, "esp_event_loop_create_default");
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// 存储
	mountFlash();
	mountMem();
	mountSd();

	tree(PerfixRoot, 4);

	// wifi
	nvsInit();
	wifiInit(true);

	wifiStart();
	auto sta = wifiStationGetInfo();
	wifiStationStart();
	wifiConnect((const char*)sta.ssid, (const char*)sta.password);
	while (wifiIsWantConnect() && !wifiIsConnect())
		vTaskDelay(100);

	if (!wifiIsConnect())
	{
		// 关闭wifi启动AP
		wifiStationStop();
		auto ap = wifiApGetInfo();
		ESP_LOGI(TAG, "load ap ssid: %s, password: %s", sta.ssid, sta.password);
		if (ap.ssid_len == 0)
			wifiApSet("esp32p4", "12345678");
		wifiApStart();
	}

	// 服务器
	ESP_LOGI(TAG, "serverStart");
	serverStart();
	ESP_LOGI(TAG, "serverStart done");
}
