#include "aiAgentApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "app/appStackManager.hpp"
#include "task/task.hpp"
#include "aiAgent/aiClient.hpp"
#include "aiAgent/aiParser.hpp"
#include "aiAgent/aiExecutor.hpp"
#include "storage/fat.hpp"
#include "battery/batteryManager.hpp"

// wifi 函数前向声明（避免直接包含 wifi.hpp 触发默认参数重定义）
bool wifiIsConnect();
esp_ip4_addr_t wifiStationGetIp();

#include <esp_log.h>
#include <cstring>
#include <cstdio>

// ============================================================
//  构造 / 析构
// ============================================================

AiAgentApp::AiAgentApp(Display* display)
	: App(display)
{
}

AiAgentApp::~AiAgentApp() = default;

// ============================================================
//  init / deinit
// ============================================================

void AiAgentApp::init()
{
	App::init();

	// 加载 AI 配置
	m_config = aiConfigLoad();

	auto guard = display->lockGuard();
	if (!guard) {
		ESP_LOGE(TAG, "无法锁定显示");
		return;
	}

	lv_obj_set_style_bg_color(screen, GUI::Color::BG, 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	buildUi();

	// 如果配置有效，显示就绪提示
	if (m_config.valid) {
		addChatEntry("system", "AI 助手已就绪，请输入指令");
	} else {
		addChatEntry("system",
			"⚠️ AI 未配置\n"
			"请将 API Key 写入 NVS (ai_agent/ai_api_key)\n"
			"并在 /root/system/ai_config.json 中配置 API 地址");
	}
}

void AiAgentApp::deinit()
{
	App::deinit();
}

void AiAgentApp::onForeground()
{
	App::onForeground();
	updateContextHint();
}

// ============================================================
//  UI 构建
// ============================================================

void AiAgentApp::buildUi()
{
	// ── 顶部栏：返回 ──
	auto topBar = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_left(topBar, 16, 0);
	lv_obj_set_style_pad_right(topBar, 16, 0);
	lv_obj_set_style_pad_top(topBar, 8, 0);

	m_backBtn = GUI::createButton(topBar, "\u2190 \u8fd4\u56de", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	auto title = GUI::createLabel(topBar, "AI \u52a9\u624b");
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// ── 上下文提示 ──
	m_contextHint = lv_label_create(screen);
	lv_obj_set_width(m_contextHint, lv_pct(96));
	lv_obj_set_style_text_color(m_contextHint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_contextHint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_label_set_text(m_contextHint, "");
	lv_obj_set_style_pad_left(m_contextHint, 16, 0);
	lv_obj_set_style_pad_bottom(m_contextHint, 4, 0);

	// ── 对话记录（使用 textarea 只读模式） ──
	m_chatLog = lv_textarea_create(screen);
	lv_obj_set_width(m_chatLog, lv_pct(100));
	lv_obj_set_flex_grow(m_chatLog, 1);
	lv_textarea_set_text(m_chatLog, "");
	lv_obj_add_flag(m_chatLog, LV_OBJ_FLAG_CLICK_FOCUSABLE);
	lv_obj_set_style_border_width(m_chatLog, 0, 0);
	lv_obj_set_style_bg_color(m_chatLog, GUI::Color::CARD, 0);
	lv_obj_set_style_bg_opa(m_chatLog, LV_OPA_30, 0);
	lv_obj_set_style_pad_all(m_chatLog, 8, 0);
	lv_obj_set_style_text_color(m_chatLog, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_chatLog, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_textarea_set_cursor_click_pos(m_chatLog, false);
	lv_obj_add_flag(m_chatLog, LV_OBJ_FLAG_SCROLLABLE);

	// ── Loading 标签 ──
	m_loadingLabel = lv_label_create(screen);
	lv_obj_set_width(m_loadingLabel, lv_pct(100));
	lv_label_set_text(m_loadingLabel, "");
	lv_obj_set_style_text_align(m_loadingLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(m_loadingLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_loadingLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(m_loadingLabel, 4, 0);
	lv_obj_set_style_pad_bottom(m_loadingLabel, 4, 0);

	// ── 输入行 ──
	auto inputRow = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_left(inputRow, 8, 0);
	lv_obj_set_style_pad_right(inputRow, 8, 0);
	lv_obj_set_style_pad_top(inputRow, 4, 0);
	lv_obj_set_style_pad_bottom(inputRow, 4, 0);

	m_inputField = lv_textarea_create(inputRow);
	lv_obj_set_flex_grow(m_inputField, 1);
	lv_obj_set_height(m_inputField, 48);
	lv_textarea_set_placeholder_text(m_inputField, "\u8bf4\u4e00\u53e5\u8bdd\uff0c\u8ba9 AI \u5e2e\u4f60\u64cd\u4f5c\u8bbe\u5907...");
	lv_obj_set_style_radius(m_inputField, 8, 0);
	lv_obj_set_style_bg_color(m_inputField, LV_COLOR_MAKE(0x2d, 0x2d, 0x3d), 0);
	lv_obj_set_style_bg_opa(m_inputField, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_inputField, 1, 0);
	lv_obj_set_style_border_color(m_inputField, LV_COLOR_MAKE(0x44, 0x44, 0x55), 0);

	m_sendBtn = GUI::createButton(inputRow, "\u25b6", 56, 48);
	lv_obj_set_style_radius(m_sendBtn, 8, 0);
	lv_obj_set_style_bg_color(m_sendBtn, GUI::Color::PRIMARY, 0);
	lv_obj_add_event_cb(m_sendBtn, onSendBtnCb, LV_EVENT_CLICKED, this);

	// ── 键盘 ──
	m_keyboard = lv_keyboard_create(screen);
	lv_obj_set_size(m_keyboard, lv_pct(100), 240);
	lv_keyboard_set_textarea(m_keyboard, m_inputField);
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x1a, 0x1a, 0x2e), 0);
	lv_obj_add_event_cb(m_keyboard, onKeyboardReadyCb, LV_EVENT_READY, this);
}

// ============================================================
//  对话记录管理
// ============================================================

void AiAgentApp::addChatEntry(const std::string& role, const std::string& text)
{
	m_chatHistory.push_back("[" + role + "] " + text);

	// 更新 textarea 显示
	std::string full;
	for (const auto& line : m_chatHistory) {
		full += line + "\n";
	}
	lv_textarea_set_text(m_chatLog, full.c_str());

	// 自动滚动到底部
	lv_textarea_set_cursor_pos(m_chatLog, LV_TEXTAREA_CURSOR_LAST);
}

void AiAgentApp::setLoading(bool loading)
{
	m_isWaiting = loading;
	if (loading) {
		lv_label_set_text(m_loadingLabel, "[ \u601d\u8003\u4e2d... ]");
		lv_obj_clear_flag(m_loadingLabel, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(m_loadingLabel, "");
		lv_obj_add_flag(m_loadingLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

void AiAgentApp::updateContextHint()
{
	// 获取当前前台 App 的上下文信息
	auto* active = display->getActiveApp();
	if (active && active != this) {
		std::string ctx = active->getAiContext();
		if (!ctx.empty()) {
			// 只取第一行作为提示
			size_t nl = ctx.find('\n');
			if (nl != std::string::npos)
				ctx = ctx.substr(0, nl) + "...";
			lv_label_set_text(m_contextHint, ctx.c_str());
			lv_obj_clear_flag(m_contextHint, LV_OBJ_FLAG_HIDDEN);
			return;
		}
	}
	lv_obj_add_flag(m_contextHint, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
//  默认 System Prompt（文件不存在时的 fallback）
// ============================================================

constexpr const char* DEFAULT_SYSTEM_PROMPT = R"(
你是一个 ESP32-P4 游戏主机的 AI 助手。
你必须以 JSON 格式返回操作指令，格式如下：

{"actions":[{"action":"...","text":"...",...}]}

系统级操作（始终可用）：
- respond_text: 显示文字回复. 参数: text
- open_app: 打开应用. 参数: app (tetris/snake/fruitNinja/chineseChess)
- navigate_back: 返回上一级
- navigate_home: 回桌面
- set_brightness: 调屏幕亮度. 参数: value (0-100)
- set_volume: 调音量. 参数: value (0-100)
- play_audio: 播放音频文件. 参数: text (文件路径)
- wifi_connect: 连接 WiFi. 参数: params.ssid, params.password
- system_info: 显示设备信息
- show_notification: 显示通知. 参数: text, params.type (info/success/error)
- power_off: 关机（会弹确认）

App 专用操作通过 app_custom 路由：
{"action":"app_custom","command":"xxx","params":{...}}

注意：
1. 如果只需要回复用户文字，使用 respond_text
2. 如果需要执行多个操作，全部放入 actions 数组，按顺序执行
3. 如果用户要求打开某个应用，使用 open_app
4. 返回的 JSON 必须合法，不能包含多余文字

{app_context}
)";

// ============================================================
//  发送逻辑
// ============================================================

void AiAgentApp::onSend()
{
	auto guard = display->lockGuard();

	if (m_isWaiting) return;

	ESP_LOGI(TAG, "onSend: api_url=%s, model=%s, has_key=%d",
		m_config.apiUrl.c_str(), m_config.model.c_str(), m_config.valid);

	// 防重
	if (xTaskGetTickCount() < m_nextActionTime) return;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;

	// 获取输入（立即拷贝！lv_textarea_get_text 返回内部指针，清空后会变为野指针）
	const char* inputPtr = lv_textarea_get_text(m_inputField);
	if (!inputPtr || inputPtr[0] == '\0') return;
	std::string input(inputPtr);

	ESP_LOGI(TAG, "用户输入: %s", input.c_str());

	// 断开键盘 → 清空输入框 → 移除焦点
	lv_keyboard_set_textarea(m_keyboard, nullptr);
	lv_textarea_set_text(m_inputField, "");
	lv_obj_clear_state(m_inputField, LV_STATE_FOCUSED);

	// 加入对话记录
	addChatEntry(">>", input);

	// 检查配置
	if (!m_config.valid) {
		addChatEntry("!", "AI 未配置，请先设置 API Key");
		return;
	}

	// 开始 loading
	setLoading(true);

	// 获取前台 App 上下文
	std::string appContext;
	auto* active = display->getActiveApp();
	if (active && active != this) {
		appContext = active->getAiContext();
		ESP_LOGI(TAG, "前台 App 上下文: %s", appContext.c_str());
	}

	// 读取 system prompt（文件存在则从文件读取，否则用内置默认值）
	std::string systemPrompt;
	{
		FILE* f = fopen("/root/system/ai_prompt.txt", "r");
		if (f) {
			fseek(f, 0, SEEK_END);
			long len = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (len > 0) {
				systemPrompt.resize(len);
				fread(systemPrompt.data(), 1, len, f);
				ESP_LOGI(TAG, "从文件加载 system prompt (%ld 字节)", len);
			}
			fclose(f);
		}
		if (systemPrompt.empty()) {
			systemPrompt = DEFAULT_SYSTEM_PROMPT;
			ESP_LOGI(TAG, "使用内置默认 system prompt");
		}
	}

	// 替换 {app_context}
	{
		size_t pos = systemPrompt.find("{app_context}");
		if (pos != std::string::npos) {
			systemPrompt.replace(pos, 13, appContext.empty() ? "当前 App 无自定义操作" : appContext);
		} else if (!appContext.empty()) {
			systemPrompt += "\n\n当前应用上下文：\n" + appContext;
		}
	}

	// 发送请求
	struct AiSendCtx { AiAgentApp* self; std::string text; std::string err; bool ok; };
	HttpClient::send(m_config, systemPrompt, input,
		[this](bool success, const std::string& responseText, const std::string& errorMsg)
		{
			auto* ctx = new AiSendCtx{ this, responseText, errorMsg, success };
			Task::addTask([](void* param) -> TickType_t {
				auto* c = static_cast<AiSendCtx*>(param);
				c->self->onAiResponse(c->ok, c->text, c->err);
				delete c;
				return Task::infinityTime;
			}, "aiRsp", ctx, 0, Task::Affinity::None);
		});
}

void AiAgentApp::onAiResponse(bool success, const std::string& responseText, const std::string& errorMsg)
{
	// 此函数在 daemon Task 中运行，所有 LVGL 操作须持有锁
	auto guard = display->lockGuard();

	ESP_LOGI(TAG, "onAiResponse: success=%d, textLen=%zu, err=%s",
		success, responseText.size(), errorMsg.c_str());

	setLoading(false);

	// 重新连接键盘到输入框，允许继续输入
	lv_keyboard_set_textarea(m_keyboard, m_inputField);

	if (!success) {
		addChatEntry("!", "请求失败: " + errorMsg);
		return;
	}

	// 解析
	AiResponse response = AiParser::parse(responseText);

	if (!response.success && response.actions.empty()) {
		addChatEntry("!", "解析失败: " + response.errorMsg);
		// 显示原始响应内容方便调试
		if (!response.rawContent.empty()) {
			std::string raw = response.rawContent;
			if (raw.size() > 500) raw = raw.substr(0, 500) + "...";
			addChatEntry("RAW", raw);
		}
		return;
	}

	// 收集需要 UI 处理的 action（respond_text 等）
	std::string replyText;
	bool hasSystemAction = false;

	for (const auto& action : response.actions) {
		switch (action.type) {
			case AiActionType::RespondText:
				if (!action.text.empty()) {
					if (!replyText.empty()) replyText += "\n";
					replyText += action.text;
				}
				break;
			case AiActionType::SystemInfo: {
				// Agent 模式：采集数据后二次调用 AI，由 AI 自然语言描述
				hasSystemAction = true;
				break;
			}
			case AiActionType::ShowNotification:
				hasSystemAction = true;
				break;
			case AiActionType::PowerOff:
				hasSystemAction = true;
				// TODO: 确认后 Deep-sleep
				break;
			case AiActionType::ParseError:
				if (!action.text.empty()) {
					if (!replyText.empty()) replyText += "\n";
					replyText += "[!] " + action.text;
				}
				break;
			default:
				break;
		}
	}

	// 如果 AI 请求了 system_info，走 Agent 模式：采集数据 → 二次调用 AI
	if (hasSystemAction) {
		handleSystemInfo();
		return;
	}

	// 显示 AI 文本回复
	if (!replyText.empty()) {
		addChatEntry("AI", replyText);
	}

	// 执行动作（除了已 UI 处理的）
	auto* mgr = display->getStackManager();
	auto execResult = AiExecutor::execute(response.actions, display, mgr);

	// 如果 AI 没有文本回复但有执行结果，显示摘要
	if (replyText.empty() && execResult.totalCount > 0) {
		addChatEntry("AI", execResult.summary);
	}
}

// ════════════════════════════════════════════════════════════════
//  Agent: system_info
// ════════════════════════════════════════════════════════════════

void AiAgentApp::handleSystemInfo()
{
	addChatEntry(">>", "(采集系统信息...)");

	// 1. 采集数据
	std::string data;
	{
		int batPct = BatteryManager::instance().getPercent();
		if (batPct >= 0) {
			data += "- 电池电量: " + std::to_string(batPct) + "%";
			data += BatteryManager::instance().isCharging() ? " (充电中)" : "";
			data += "\n";
		}
		if (wifiIsConnect()) {
			esp_ip4_addr_t ip = wifiStationGetIp();
			char ipStr[16];
			snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&ip));
			data += "- IP 地址: " + std::string(ipStr) + "\n";
			data += "- WiFi: 已连接\n";
		} else {
			data += "- WiFi: 未连接\n";
		}
		data += "- 屏幕分辨率: 720×1280\n";
		data += "- 型号: ESP32-P4\n";
	}

	setLoading(true);

	// 2. 把数据喂给 AI，让它自然语言描述
	HttpClient::send(m_config, R"(你是设备的系统状态报告员。
请根据以下系统数据，用自然语言向用户描述设备的当前状态，语气友好。
只返回 respond_text 动作，不要执行其他操作。)",
		data,
		[this](bool ok, const std::string& resp, const std::string& err) {
			Task::addTask([](void* p) -> TickType_t {
				auto* args = static_cast<std::tuple<AiAgentApp*, bool, std::string, std::string>*>(p);
				auto* self = std::get<0>(*args);
				bool ok2 = std::get<1>(*args);
				auto& resp2 = std::get<2>(*args);
				auto& err2 = std::get<3>(*args);

				auto guard = self->display->lockGuard();
				self->setLoading(false);
				lv_keyboard_set_textarea(self->m_keyboard, self->m_inputField);

				if (!ok2) {
					self->addChatEntry("!", "采集信息失败: " + err2);
				} else {
					// 解析 AI 对系统信息的描述
					auto response = AiParser::parse(resp2);
					if (!response.success && response.actions.empty()) {
						self->addChatEntry("!", "解析失败: " + response.errorMsg);
					} else {
						for (const auto& a : response.actions) {
							if (a.type == AiActionType::RespondText && !a.text.empty()) {
								self->addChatEntry("AI", a.text);
							}
						}
					}
				}
				delete args;
				return Task::infinityTime;
			}, "aiSysInfo", new std::tuple(this, ok, resp, err), 0, Task::Affinity::None);
		});
}

// ============================================================
//  手柄输入
// ============================================================

void AiAgentApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	uint16_t newPress = state.buttons;

	// B：返回
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
		if (xTaskGetTickCount() < m_nextActionTime) return;
		m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;

		Task::addTask([](void* param) -> TickType_t {
			auto* self = static_cast<AiAgentApp*>(param);
			if (self->m_manager) self->m_manager->pop();
			return Task::infinityTime;
		}, "aiBack", this, 0, Task::Affinity::None);
		return;
	}

	// A / START：发送
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_START))) {
		onSend();
	}
}

// ============================================================
//  LVGL 事件回调
// ============================================================

void AiAgentApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<AiAgentApp*>(lv_event_get_user_data(e));
	Task::addTask([](void* param) -> TickType_t {
		auto* app = static_cast<AiAgentApp*>(param);
		if (app->m_manager) app->m_manager->pop();
		return Task::infinityTime;
	}, "aiBack", self, 0, Task::Affinity::None);
}

void AiAgentApp::onSendBtnCb(lv_event_t* e)
{
	auto* self = static_cast<AiAgentApp*>(lv_event_get_user_data(e));
	self->onSend();
}

void AiAgentApp::onKeyboardReadyCb(lv_event_t* e)
{
	// 键盘确认键（Enter）→ 发送
	auto* self = static_cast<AiAgentApp*>(lv_event_get_user_data(e));
	self->onSend();
}
