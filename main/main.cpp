#include "display/display.hpp"
#include "display/ili9881c.hpp"
#include "display/font.hpp"
#include "touch/touch.hpp"
#include "touch/gt911Indev.hpp"

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
#include "websocket/wsServer.hpp"
#include "host/wsInputHandler.hpp"
#include "host/wsConsoleHandler.hpp"
#include "host/udpInput.hpp"
#include "wifi/mdns.hpp"
#include "virtualIndev/wsTouchHandler.hpp"
#include "screenStream/wsStreamHandler.hpp"
#include "host/wsInputHandler.hpp"
#include "host/wsConsoleHandler.hpp"
#include "audio/ES8311.hpp"
#include "audio/Audio.hpp"

#include "gamepad/bleGamepad.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"
#include "monitor/systemMonitor.hpp"
#include "battery/batteryManager.hpp"
#include "battery/batteryMonitor.hpp"
#include "time/timeService.hpp"
#include "elfSdk/elf_sdk.h"
#include "ota/otaManager.hpp"

static constexpr char TAG[] = "main";

Display display{};
IIC iic;
Touch touch{};
ES8311 audio{};
AppStackManager stackManager{};

extern "C" void app_main(void)
{
	Task::init(2);

	Task::addTask([](void*)->TickType_t { srand(esp_random()); return 60 * 1000; }, "srand", nullptr, 0, Task::Affinity::None);

	Task::addTask([](void*)->TickType_t { setenv("TZ", "CST-8", 1); tzset();return Task::infinityTime;}, "setting time zone", nullptr, 0, Task::Affinity::None);

	// 系统监视器（串口输出，每 1 秒采样一次）：CPU 利用率 + 内存（内部 RAM/PSRAM）+ 显示 FPS
	SystemMonitor::instance().setBrief(true);
	SystemMonitor::instance().start(&display);

	ESP_LOGI(TAG, "esp_event_loop_create_default");
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// 存储
	mountFlash();
	mountMem();

	Task::addTask([](void*)->TickType_t{ mountSd(); return Task::infinityTime; }, "mount sd", nullptr, 0, Task::Affinity::None);

	// nvs
	nvsInit();

	// ELF 插件 SDK：注册符号表（C 库 / C++ 运行时 / C++ 框架）——必须在任何 dlopen 之前
	elf_sdk_init();

	// 早期启动 C6 协处理器
	Slave::instance().start();

	// 计算帧缓冲区数量
	constexpr auto tearAvoidMode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI;
	constexpr auto rotation = ESP_LV_ADAPTER_ROTATE_90;
	const auto frameBufferCount = esp_lv_adapter_get_required_frame_buffer_count(tearAvoidMode, rotation);

	constexpr auto horizontalResolution = 720;
	constexpr auto verticalResolution = 1280;

	// 初始化LVGL
	if (!display.init())
	{
		ESP_LOGE(TAG, "Failed to initialize display");
		return;
	}

	// OTA 管理：延迟 10s 检测上次升级是否回滚（NVS 记录）。不自动确认固件——
	// 回滚确认改全手动：OTA 后固件处于 PENDING_VERIFY，用户在 web 页面
	// 「确认此固件」（POST /api/ota/confirm）才关闭回滚窗口；未确认则任何重启自动回退。
	// ⚠ otaInit 内部会 mmap flash（禁用 cache 期间要求栈在内部 RAM），而 Task daemon 栈在
	//   PSRAM → 必须经 otaRunInInternalStack 在内部 RAM 栈任务中执行。
	Task::addTask([](void*) -> TickType_t {
		otaRunInInternalStack([](void*) { otaInit(); }, nullptr);
		return Task::infinityTime;
	}, "ota init", nullptr, 10000, Task::Affinity::None);

	// 初始化ILI9881c
	if (!ILI9881c::getInstance().init(horizontalResolution, verticalResolution, frameBufferCount))
	{
		ESP_LOGE(TAG, "Failed to initialize ILI9881c hardware");
		return;
	}

	display.bindDisplay(ILI9881c::getInstance().getPanel(), ILI9881c::getInstance().getPanelIo(), horizontalResolution, verticalResolution, tearAvoidMode, rotation);
	iic = IIC{ {GPIO_NUM_8}, {GPIO_NUM_7} };
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
		GT911Indev::instance().start(&display, &touch);

	// 启动 LVGL 工作任务
	if (!display.start())
	{
		ESP_LOGE(TAG, "Failed to start LVGL adapter");
		return;
	}
	display.setFpsStatisticsEnabled();

	// 启动屏幕流模块（使用 ESP32-P4 硬件 JPEG 编码器）
	ScreenStream::instance().start(&display, horizontalResolution, verticalResolution);

	// 加载字体并设为默认
	if (testFile("/root/system/NotoSC.ttf"))
	{
		FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Small), FontLoader::FontSize::Small);
		FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Medium), FontLoader::FontSize::Medium);
		FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Large), FontLoader::FontSize::Large);

		// 字体回退链：NotoSC 缺少的符号（电池图标等）由 LVGL 内置 symbol 字体提供
		const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Small))->fallback = lv_font_get_default();
		const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Medium))->fallback = lv_font_get_default();
		const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Large))->fallback = lv_font_get_default();
	}
	else
	{
		ESP_LOGE(TAG, "缺失字体/root/system/NotoSC.ttf");
		FontLoader::setDefault(lv_font_get_default(), FontLoader::FontSize::Small);
		FontLoader::setDefault(lv_font_get_default(), FontLoader::FontSize::Medium);
		FontLoader::setDefault(lv_font_get_default(), FontLoader::FontSize::Large);
	}

	// 启动虚拟触摸输入（用于从 web 注入触摸事件）
	Task::addTask([](void* param)->TickType_t{VirtualIndev::instance().start((Display*)param); return Task::infinityTime;}, "starting virtual indev", &display, 0, Task::Affinity::None);

	// 音频
	if (audio.init(iic, {
		.i2s_mck = GPIO_NUM_13,
		.i2s_bck = GPIO_NUM_12,
		.i2s_ws = GPIO_NUM_10,
		.i2s_dout = GPIO_NUM_9,
		.pa_pin = GPIO_NUM_53,
		}))
	{
		audio.setVolume(100);
	}

	// 初始化音频管理器
	if (!Audio::instance().init(audio))
		ESP_LOGE(TAG, "音频管理器初始化失败");
	else
		Audio::loadVolumeFromNvs();

	// 初始化主机电池 ADC（全局一次性）
	BatteryManager::instance().init();

	// 启动 AppStackManager — 多栈导航系统
	stackManager = std::move(AppStackManager{ &display });
	display.setStackManager(&stackManager);

	// 创建根栈（索引 0）并推入桌面应用
	stackManager.createStack();
	DesktopApp* desktop = new DesktopApp{ &display };
	stackManager.push(desktop);

	// 启动 BLE 手柄管理器
	ESP_LOGI(TAG, "BleGamepad start");
	BleGamepad::instance().start(&display);
	ESP_LOGI(TAG, "BleGamepad start done");

	// 启动 AI 助手浮动 Overlay（驻留在 lv_layer_sys 上，不影响 AppStack）
	AiAgentOverlay::instance().start(&display);

	// 手势检测由 AiAgentOverlay 内部通过 LVGL 事件完成（无需后台 Task）

	// 低电量检测提醒（主机 + 蓝牙手柄，lv_layer_sys 弹窗 + 提示音）
	BatteryMonitor::instance().start(&display);

	// wifi
	wifiInit(true);

	wifiStart();
	wifiNatSetAutoStart(true);

	// 全局时间同步服务（SNTP 一次性初始化，WiFi 拿到 IP 时自动同步）
	TimeService::instance().start();

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

	{
		const char* hostname = mdnsGetConfiguredHostname();
		char instanceName[128];
		char httpInstance[128];
		char wsInstance[128];

		snprintf(instanceName, sizeof(instanceName), "%s Game Console", hostname);
		snprintf(httpInstance, sizeof(httpInstance), "%s HTTP", hostname);
		snprintf(wsInstance, sizeof(wsInstance), "%s WS", hostname);

		mdnsStart(hostname, instanceName);

		ESP_LOGI(TAG, "mdnsServiceAdd");
		mdnsServiceAdd(httpInstance, "_http", "_tcp", 80);
		mdnsServiceAdd(wsInstance, "_ws", "_tcp", 8080);
		mdnsServiceAdd(instanceName, "_p4console", "_tcp", 80);
	}
	ESP_LOGI(TAG, "mdnsInit done");

	// 服务器
	ESP_LOGI(TAG, "serverStart");
	serverStart();
	ESP_LOGI(TAG, "serverStart done");

	// WebSocket 服务器（端口 8080）
	ESP_LOGI(TAG, "wsServerStart");
	wsServerStart();
	wsServerRegisterWs("/ws/touch", wsTouchHandler);
	wsServerRegisterWs("/ws/stream", wsStreamHandler);
	wsServerRegisterWs("/ws/input", wsInputHandler);
	wsServerRegisterWs("/ws/console", wsConsoleHandler);
	ESP_LOGI(TAG, "wsServerStart done");

	// UDP 游戏手柄输入（端口 8082，替代 WebSocket 的低延迟方案）
	udpInputStart();
	ESP_LOGI(TAG, "udpInputStart done");
}
