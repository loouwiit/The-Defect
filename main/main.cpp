#include "display/display.hpp"
#include "display/ili9881c.hpp"
#include "display/font.hpp"
#include "touch/touch.hpp"

#include <esp_log.h>
#include <cstring>

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

#include "app/appStackManager.hpp"
#include "app/desktopApp/desktopApp.hpp"
#include "screenStream/screenStream.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "gamepadIndev/gamepadIndev.hpp"
#include "wsServer/wsServer.hpp"
#include "wifi/mdns.hpp"
#include "audio/ES8311.hpp"
#include "audio/Audio.hpp"

#include "bleGamepad/bleGamepad.hpp"

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

	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	// Touch touch{ iic, {GPIO_NUM_46} };
	// display.bindTouch(touch.getHandle());

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

	// 启动手柄 KEYPAD indev（4 玩家独立 indev，由 BleGamepad 注入）
	GamepadIndev::instance().start(&display);

	// 音频
	ES8311 audio{};
	if (audio.init(iic, {
		.i2s_mck = GPIO_NUM_13,
		.i2s_bck = GPIO_NUM_12,
		.i2s_ws = GPIO_NUM_10,
		.i2s_dout = GPIO_NUM_9,
		.pa_pin = GPIO_NUM_53,
		}))
	{
		audio.setVolume(100);
		esp_codec_dev_sample_info_t config{};
		config.bits_per_sample = 16;
		config.channel = 2;
		config.sample_rate = 48000;
		audio.open(&config);
	}

	// 初始化音频管理器
	if (!Audio::instance().init(audio))
		ESP_LOGE(TAG, "音频管理器初始化失败");

	if constexpr (false) {
		// BGM1 — 循环，绑定生命周期
		auto bgm1 = Audio::play("/root/sd/new/music/aac_32k/halcyon.aac").setLoop(true);
		bgm1.setVolume(1.0f);
		bgm1.play();              // 开始播放

		// BGM2 — 不循环，手动控制生命周期
		auto bgm2 = new AudioHandle{ Audio::play("/root/sd/new/music/aac_32k/To My Dear Friends.aac") };
		bgm2->play();
		Task::addTask([](void* param)->TickType_t
			{
				auto& bgm2 = *static_cast<AudioHandle*>(param);
				if (!bgm2.isPlaying())
				{
					delete static_cast<AudioHandle*>(param);
					return Task::infinityTime;
				}
				float volume = bgm2.getVolume() > 0.7f ? 0.1f : 1.0f;
				ESP_LOGI(TAG, "BGM2 volume: %.2f", volume);
				bgm2.setVolume(volume);
				return 3000;
			}, "bgm2", bgm2, 0, Task::Affinity::None);

		// 解绑生命周期，自动 play()
		Audio::play("/root/sd/new/music/aac_32k/清水準一 - Bloom of Youth (风华正茂).aac").setVolume(1.0f).detach();

		vTaskDelay(10 * 1000);
	}

	// 启动 AppStackManager — 多栈导航系统
	AppStackManager stackManager(&display);
	display.setStackManager(&stackManager);

	// 创建根栈（索引 0）并推入桌面应用
	stackManager.createStack();
	DesktopApp* desktop = new DesktopApp{ &display };
	stackManager.push(desktop);

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

	// 启动 BLE 手柄管理器
	ESP_LOGI(TAG, "BleGamepad start");
	BleGamepad::instance().start(&display);
	ESP_LOGI(TAG, "BleGamepad start done");

	// 测试：延迟几秒后自动连接指定的手柄 MAC
	// NimBLE 使用小端序
	static constexpr uint8_t TARGET_BDA[2][6] = { {0xC2,0x04,0x8E,0x3B,0xDA,0xEC },{ 0xCE, 0x93, 0x8C, 0xBD, 0x4D, 0x74 } };

	Task::addTask([](void* param)->TickType_t
		{
			auto& bg = BleGamepad::instance();
			auto& id = *(size_t*)param;
			const auto devices = bg.getScannedDevices();

			if (id == devices.size())
			{
				delete& id;
				return Task::infinityTime;
			}

			ESP_LOGI(TAG, "Auto-connect check: %zu / %zu devices", id, devices.size());

			ESP_LOGI(TAG, "  [%zu] %s [%02x:%02x:%02x:%02x:%02x:%02x]",
				id, devices[id].name,
				devices[id].bda[0], devices[id].bda[1], devices[id].bda[2],
				devices[id].bda[3], devices[id].bda[4], devices[id].bda[5]);
			for (size_t j = 0; j < sizeof(TARGET_BDA) / sizeof(TARGET_BDA[0]); j++)
				if (memcmp(devices[id].bda, TARGET_BDA[j], 6) == 0)
				{
					ESP_LOGI(TAG, "Auto-connecting to: %s", devices[id].name);
					bg.connect(id);
					id++;
					return 5000; // 5000ms后再进行第二次判断连接，给予充足时间
				}

			id++;
			return 1;
		}, "bleAutoConnect", new size_t{ 0 }, 3000, Task::Affinity::None); // 延迟300ms

	// 主循环 — stackManager 和所有 app 在栈上自动生命周期管理
	while (true)
		vTaskDelay(1000);
}
