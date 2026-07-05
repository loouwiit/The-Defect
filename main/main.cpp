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
#include <esp_random.h>

#include "task/task.hpp"
#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "wifi/slave.hpp"
#include "server/serverKernal.hpp"

#include "storage/fat.hpp"
#include "storage/mem.hpp"
#include "storage/sd.hpp"

#include "app/appStackManager.hpp"
#include "app/desktopApp/desktopApp.hpp"
#include "screenStream/screenStream.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "wsServer/wsServer.hpp"
#include "wifi/mdns.hpp"
#include "audio/ES8311.hpp"
#include "audio/Audio.hpp"

#include "bleGamepad/bleGamepad.hpp"
#include "monitor/cpuMonitor.hpp"
#include "battery/batteryManager.hpp"

static constexpr char TAG[] = "main";

extern "C" void app_main(void)
{
	Task::init(2);

	Task::addTask([](void*)->TickType_t { srand(esp_random()); return 60 * 1000; }, "srand");

	// CPU 利用率监视器（串口输出，每 1 秒采样一次）
	CpuMonitor::instance().setBrief(true);
	CpuMonitor::instance().start();

	ESP_LOGI(TAG, "esp_event_loop_create_default");
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// 存储
	mountFlash();
	mountMem();
	mountSd();

	// nvs
	nvsInit();

	// 早期启动 C6 协处理器
	Slave::instance().start();

	// 计算帧缓冲区数量
	constexpr auto tearAvoidMode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI;
	constexpr auto rotation = ESP_LV_ADAPTER_ROTATE_90;
	const auto frameBufferCount = esp_lv_adapter_get_required_frame_buffer_count(tearAvoidMode, rotation);

	constexpr auto horizontalResolution = 720;
	constexpr auto verticalResolution = 1280;

	// 初始化LVGL
	Display display;
	if (!display.init())
	{
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
	Touch touch{};
	if (iic.detect(Touch::Address))
	{
		ESP_LOGI(TAG, "Touch controller found at address 0x%02X", Touch::Address);
		touch = { iic, {GPIO_NUM_46}, {GPIO_NUM_47}, Touch::Address };
	}
	else if (iic.detect(Touch::AddressAlternative))
	{
		ESP_LOGI(TAG, "Touch controller found at alternative address 0x%02X", Touch::AddressAlternative);
		touch = { iic, {GPIO_NUM_46}, {GPIO_NUM_47}, Touch::AddressAlternative };
	}
	else
		ESP_LOGE(TAG, "Touch controller not found");

	if (touch.getHandle() != nullptr)
		display.bindTouch(touch.getHandle());

	// 启动 LVGL 工作任务
	if (!display.start())
	{
		ESP_LOGE(TAG, "Failed to start LVGL adapter");
		return;
	}
	display.setFpsStatisticsEnabled();

	Task::addTask([](void* param)->TickType_t
		{
			auto& display = *static_cast<Display*>(param);
			ESP_LOGI(TAG, "FPS: %u", display.getFps());
			return 1000;
		}, "fpsMonitor", &display, 1000, Task::Affinity::None);

	// 启动屏幕流模块（使用 ESP32-P4 硬件 JPEG 编码器）
	ScreenStream::instance().start(&display, horizontalResolution, verticalResolution);

	// 加载字体并设为默认
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Default));
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Large), FontLoader::FontSize::Large);
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Small), FontLoader::FontSize::Small);

	// 字体回退链：NotoSC 缺少的符号（电池图标等）由 LVGL 内置 symbol 字体提供
	const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Small))->fallback = lv_font_get_default();
	const_cast<lv_font_t*>(FontLoader::getDefault())->fallback = lv_font_get_default();
	const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Large))->fallback = lv_font_get_default();

	// 启动虚拟触摸输入（用于从 web 注入触摸事件）
	VirtualIndev::instance().start(&display);

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
	else
		Audio::loadVolumeFromNvs();

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

	// 初始化主机电池 ADC（全局一次性）
	BatteryManager::instance().init();

	// 启动 AppStackManager — 多栈导航系统
	AppStackManager stackManager(&display);
	display.setStackManager(&stackManager);

	// 创建根栈（索引 0）并推入桌面应用
	stackManager.createStack();
	DesktopApp* desktop = new DesktopApp{ &display };
	stackManager.push(desktop);

	// 启动 BLE 手柄管理器
	ESP_LOGI(TAG, "BleGamepad start");
	BleGamepad::instance().start(&display);
	ESP_LOGI(TAG, "BleGamepad start done");

	// wifi
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

	// 主循环 — stackManager 和所有 app 在栈上自动生命周期管理
	while (true)
		vTaskDelay(1000);
}
