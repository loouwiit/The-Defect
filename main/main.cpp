#include "display/display.hpp"
#include "display/ili9881c.hpp"
#include "display/font.hpp"
#include "touch/touch.hpp"

#include <esp_log.h>
#include <cmath>

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

#include "app/desktopApp/desktopApp.hpp"
#include "screenStream/screenStream.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "wsServer/wsServer.hpp"
#include "wifi/mdns.hpp"
#include "audio/audioManager.hpp"

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

	// 计算帧缓冲区数量
	constexpr auto tearAvoidMode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI;
	constexpr auto rotation = ESP_LV_ADAPTER_ROTATE_90;
	const auto frameBufferCount = esp_lv_adapter_get_required_frame_buffer_count(tearAvoidMode, rotation);

	constexpr auto horizontalResolution = 720;
	constexpr auto verticalResolution = 1280;

	// 初始化LVGL
	Display display;
	if (!display.init()) {
		ESP_LOGE(TAG, "Failed to initialize display");
		return;
	}

	// 初始化ILI9881c
	if (!ILI9881c::getInstance().init(horizontalResolution, verticalResolution, frameBufferCount))
	{
		ESP_LOGE(TAG, "Failed to initialize ILI9881c hardware");
		return;
	}

	display.bindDisplay(ILI9881c::getInstance().getPanel(), ILI9881c::getInstance().getPanelIo(), horizontalResolution, verticalResolution, tearAvoidMode, rotation);

	// I2C 总线（触摸和音频编解码器共用）
	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	// Touch touch{ iic, {GPIO_NUM_46} };
	// display.bindTouch(touch.getHandle());

	// 音频管理器（ES8311 编解码器 + I2S）
	ESP_LOGI(TAG, "AudioManager init");
	AudioManager::instance().init(iic).start();
	ESP_LOGI(TAG, "AudioManager init done");

	AudioManager::instance().setMasterVolume(0.5);

	// ---- 硬件隔离测试：直接向 codec 写 1kHz 正弦波 ----
	// 如果听到声音 → render/streaming 配置问题
	// 听不到声音 → I2S / ES8311 / PA / 喇叭 硬件问题
	{
		constexpr int TONE_NUM = 240;
		uint8_t* toneBuf = (uint8_t*)malloc(TONE_NUM * 4);
		for (int i = 0; i < TONE_NUM; i++) {
			float t = 2.0f * 3.14159f * 1000.0f * i / 48000.0f;
			int16_t s = (int16_t)(0.25f * 32767.0f * sinf(t));
			((int16_t*)toneBuf)[i * 2]     = s;
			((int16_t*)toneBuf)[i * 2 + 1] = s;
		}

		Task::addTask([](void* param)->TickType_t
			{
				uint8_t* buf = (uint8_t*)param;
				static int count = 0;
				if (count++ < 500) {
					AudioManager::instance().writePcmDirect(buf, 960);
					return 10;
				}
				free(buf);
				ESP_LOGI("TONE", "1kHz 测试结束 (5s)");
				vTaskDelete(nullptr);
				return 0;
			}, "tone test", toneBuf, 0, Task::Affinity::None);

		ESP_LOGW("TONE", ">>> 1kHz 正弦波持续 5 秒，有声音吗？");
	}

	// 启动 LVGL 工作任务
	if (!display.start()) {
		ESP_LOGE(TAG, "Failed to start LVGL adapter");
		return;
	}
	display.setFpsStatisticsEnabled();

	// 启动屏幕流模块（使用 ESP32-P4 硬件 JPEG 编码器）
	ScreenStream::instance().start(&display, horizontalResolution, verticalResolution);

	// 加载字体并设为默认
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Default));
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Large), FontLoader::FontSize::Large);
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Small), FontLoader::FontSize::Small);

	// 启动虚拟触摸输入（用于从 web 注入触摸事件）
	VirtualIndev::instance().start(&display);

	// 启动任务管理器
	Task::init(2);

	// 启动桌面应用
	DesktopApp* app = new DesktopApp{ &display };
	app->init();
	if (auto guard = display.lockGuard())
	{
		// 应用(v.) 应用(n.) 到屏幕
		display.applyApp(app);
	}

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

	// mdns
	ESP_LOGI(TAG, "mdnsInit");
	mdnsInit();
	mdnsStart("esp32p4", "ESP32P4 Game Console");
	ESP_LOGI(TAG, "mdnsInit done");

	ESP_LOGI(TAG, "mdnsServiceAdd");
	mdnsServiceAdd("ESP32P4 HTTP", "_http", "_tcp", 80);
	mdnsServiceAdd("ESP32P4 WS", "_ws", "_tcp", 8080);
	ESP_LOGI(TAG, "mdnsServiceAdd done");

	// 服务器
	ESP_LOGI(TAG, "serverStart");
	serverStart();
	ESP_LOGI(TAG, "serverStart done");

	// WebSocket 服务器（端口 8080，仅用于触控回传）
	ESP_LOGI(TAG, "wsServerStart");
	wsServerStart();
	ESP_LOGI(TAG, "wsServerStart done");

	// 保持栈上变量，后续移除
	while (true)
		vTaskDelay(1000);

	// cleanup (unreachable in this example, but good practice)
	delete std::exchange(app, nullptr);
}
