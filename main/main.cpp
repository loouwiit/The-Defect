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

#include "audio/ES8311.hpp"
#include "audio/Audio.hpp"
#include "audio/audioInput.hpp"

#include "gamepad/bleGamepad.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"
#include "monitor/systemMonitor.hpp"
#include "battery/batteryManager.hpp"
#include "battery/batteryMonitor.hpp"
#include "gui/systemPopup.hpp"
#include "time/timeService.hpp"
#include "elfSdk/elf_sdk.h"
#include "ota/otaManager.hpp"
#include "logService/logService.hpp"

static constexpr char TAG[] = "main";

Display display{};
IIC iic;
Touch touch{};
ES8311 audio{};

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

	// 日志重定向服务（封壳设备远程调试）：
	// esp_log_set_vprintf 接管全部 ESP_LOGx → FAT 文件 /root/system/log/app.log + /ws/log 实时推送；
	// 链式保留 UART 输出。FAT 挂载前的早期日志存 PSRAM 缓冲，此处启动后自动落盘。
	// detectCrash：复位原因 + coredump 存在性 → NVS 记录（panic 计数 / 最近崩溃时间）。
	LogService::instance().start();
	LogService::instance().detectCrash();

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
		// ⚠ 逐个判空：esp_lv_adapter_ft_font_init 失败返回 nullptr 时不能直接塞进
		//   setDefault（失败字体不得占槽），且回退链赋值前必须兜底——全部失败时
		//   getDefault(Small) 可能返回 nullptr，->fallback 解引用即崩。
		const lv_font_t* smallFont = FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Small);
		const lv_font_t* mediumFont = FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Medium);
		const lv_font_t* largeFont = FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Large);
		if (!smallFont) { ESP_LOGW(TAG, "Small 字体加载失败，回退默认"); smallFont = lv_font_get_default(); }
		if (!mediumFont) { ESP_LOGW(TAG, "Medium 字体加载失败，回退默认"); mediumFont = lv_font_get_default(); }
		if (!largeFont) { ESP_LOGW(TAG, "Large 字体加载失败，回退默认"); largeFont = lv_font_get_default(); }
		FontLoader::setDefault(smallFont, FontLoader::FontSize::Small);
		FontLoader::setDefault(mediumFont, FontLoader::FontSize::Medium);
		FontLoader::setDefault(largeFont, FontLoader::FontSize::Large);

		// 字体回退链：NotoSC 缺少的符号（电池图标等）由 LVGL 内置 symbol 字体提供
		if (auto* f = const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Small)))
			f->fallback = lv_font_get_default();
		if (auto* f = const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Medium)))
			f->fallback = lv_font_get_default();
		if (auto* f = const_cast<lv_font_t*>(FontLoader::getDefault(FontLoader::FontSize::Large)))
			f->fallback = lv_font_get_default();
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
		.i2s_din = GPIO_NUM_11,
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

	// 初始化录音服务（麦克风 → /root/mem/voice.wav）
	if (!AudioInput::instance().init(&audio))
		ESP_LOGE(TAG, "录音服务初始化失败");

	// 初始化主机电池 ADC（全局一次性）
	BatteryManager::instance().init();

	// 启动 AppStackManager — 多栈导航系统（系统级单例，程序运行期常驻）
	// ⚠ 必须用单例，不能用其他存储形式：
	//   1. RecursiveMutex 无移动语义 → 全局对象 + std::move 赋值会悬空句柄 panic（已踩过）
	//   2. 局部对象 → app_main 末尾无主循环会返回，返回时析构在后台任务
	//      （LVGL/BLE/HTTP/deleteApp）并发运行中删栈删 App → 竞态崩溃
	AppStackManager::instance().init(&display);
	display.setStackManager(&AppStackManager::instance());

	// 创建根栈（索引 0）并推入桌面应用
	AppStackManager::instance().createStack();
	DesktopApp* desktop = new DesktopApp{ &display };
	AppStackManager::instance().push(desktop);

	// 启动 BLE 手柄管理器
	ESP_LOGI(TAG, "BleGamepad start");
	BleGamepad::instance().start(&display);
	ESP_LOGI(TAG, "BleGamepad start done");

	// 启动 AI 助手浮动 Overlay（驻留在 lv_layer_sys 上，不影响 AppStack）
	AiAgentOverlay::instance().start(&display);

	// 系统弹窗服务（lv_layer_sys，队列管理，BatteryMonitor / OTA 弹窗共用）
	SystemPopup::instance().start(&display);

	// 低电量检测提醒（主机 + 蓝牙手柄，通过 SystemPopup 弹窗）
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
		ESP_LOGI(TAG, "load ap ssid: %.*s, password: %s", ap.ssid_len, ap.ssid, ap.password);
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
	// OTA 单例：首次 instance() 触发构造（创建内部 RAM 常驻任务 + 请求队列）。
	// Meyers' Singleton（magic static）保证仅构造一次且线程安全 → 无重复创建竞争，
	// 不再需要显式 init / 互斥量。此处显式调用仅确保构造发生在单线程阶段（HTTP 并发之前）。
	OtaManager::instance();

	// OTA 启动检查：延迟 10s 检测上次升级是否回滚（NVS 记录）。不自动确认固件——
	// 回滚确认改全手动：OTA 后固件处于 PENDING_VERIFY，用户在 web 页面
	// 「确认此固件」（POST /api/ota/confirm）才关闭回滚窗口；未确认则任何重启自动回退。
	// 注册点放在这里（启动流程末尾、OtaManager::instance() 之后）：
	//   - 依赖已满足（checkRollback 内部经 runInInternalStack，任务已就绪）
	//   - 注册点之前的启动段基本恒定，实际执行时刻 ≈ serverStart + 10s，比放在
	//     display.init 后（受前段启动耗时漂移影响）稳定得多
	//   - 10s 仅为错峰 flash 读取（避开启动早期字体/显示读 flash 密集期），与回滚判定无关
	// ⚠ checkRollback 内部会 mmap flash（禁用 cache 期间要求栈在内部 RAM），而 Task daemon
	//   栈在 PSRAM → 必须经 runInInternalStack 在内部 RAM 栈任务中执行。
	Task::addTask([](void*) -> TickType_t {
		OtaManager::instance().runInInternalStack(
			[](void*) { OtaManager::instance().checkRollback(); }, nullptr);

		// checkRollback 完成后弹窗提示 OTA 结果
		auto& result = OtaManager::instance().getOtaResult();
		if (result.hasEvent)
		{
			// 有 OTA 事件：升级成功或回滚
			if (result.rolledBack)
			{
				char body[256];
				snprintf(body, sizeof(body),
					"上次升级（%s）未通过验证，已自动回滚。\n当前运行 %s 版本 %s",
					result.pendingSlot, result.slot, result.version);
				SystemPopup::Options opts;
				opts.accent = GUI::Color::DANGER;
				opts.footerText = "请到 web 页面（server/ota）检查固件兼容性";
				SystemPopup::instance().show(SystemPopup::kOta,
					LV_SYMBOL_WARNING " OTA 升级失败", body, opts);
			}
			else
			{
				char body[256];
				if (result.pendingVerify)
					snprintf(body, sizeof(body),
						"已升级到 %s 版本 %s\n固件回滚窗口开启中，请到 web 页面（server/ota）确认此固件，否则重启将自动回退",
						result.slot, result.version);
				else
					snprintf(body, sizeof(body),
						"已升级到 %s 版本 %s",
						result.slot, result.version);
				SystemPopup::Options opts;
				opts.accent = GUI::Color::SUCCESS;
				opts.footerText = result.pendingVerify ? "请到 web 页面确认此固件" : "";
				SystemPopup::instance().show(SystemPopup::kOta,
					LV_SYMBOL_OK " OTA 升级成功", body, opts);
			}
		}
		else if (result.pendingVerify)
		{
			// 无新 OTA 事件，但当前固件仍待确认
			char body[256];
			snprintf(body, sizeof(body),
				"当前固件 %s 版本 %s 未确认。\n请到 web 页面（server/ota）确认此固件，否则重启将自动回退",
				result.slot, result.version);
			SystemPopup::Options opts;
			opts.accent = GUI::Color::WARNING;
			opts.footerText = "请到 web 页面确认此固件";
			SystemPopup::instance().show(SystemPopup::kOta,
				LV_SYMBOL_WARNING " 固件待确认", body, opts);
		}

		return Task::infinityTime;
		}, "ota init", nullptr, 10000, Task::Affinity::None);
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
	wsServerRegisterWs("/ws/log", logWsHandler);
	ESP_LOGI(TAG, "wsServerStart done");

	// UDP 游戏手柄输入（端口 8082，替代 WebSocket 的低延迟方案）
	udpInputStart();
	ESP_LOGI(TAG, "udpInputStart done");
}
