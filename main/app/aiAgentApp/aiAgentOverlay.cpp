#include "aiAgentOverlay.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "aiAgent/aiClient.hpp"
#include "aiAgent/aiParser.hpp"
#include "aiAgent/aiExecutor.hpp"
#include "battery/batteryManager.hpp"
#include "bleGamepad/bleGamepad.hpp"

// wifi 前向声明（避免包含 wifi.hpp 触发默认参数重定义）
bool wifiIsConnect();
esp_ip4_addr_t wifiStationGetIp();

#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <cmath>

static constexpr char TAG[] = "AiAgentOverlay";

// ============================================================
//  默认 System Prompt（文件不存在时的 fallback）
// ============================================================

const char* DEFAULT_SYSTEM_PROMPT = R"(
你是一个 ESP32-P4 游戏主机的 AI 助手。
必须以 JSON 返回操作指令。每个 action 对象中，"action" 指定操作类型，其余字段为参数。

JSON 字段说明：
- text:     通用文本（respond_text 的回复内容）
- app:      应用名（open_app 的目标应用）
- value:    数值参数（亮度/音量值 0-100）
- command:  自定义命令名（透传给前台 App）
- params:   扩展参数字典

可用系统操作：
- respond_text	回复文字	{"action":"respond_text","text":"你好"}
- open_app	打开应用	{"action":"open_app","app":"tetris"}
- navigate_back	返回上一级	{"action":"navigate_back"}
- navigate_home	回桌面	{"action":"navigate_home"}
- set_brightness	调亮度	{"action":"set_brightness","value":50}
- set_volume	调音量	{"action":"set_volume","value":70}
- play_audio	播放音频	{"action":"play_audio","text":"/path/file.mp3"}
- wifi_connect	连接WiFi	{"action":"wifi_connect","params":{"ssid":"...","password":"..."}}
- system_info	获取设备信息	{"action":"system_info"}
- show_notification	通知	{"action":"show_notification","text":"内容","params":{"type":"info"}}
- power_off	关机	{"action":"power_off"}

当前应用的自定义操作由 {app_context} 提供。

未识别的 action 会自动透传给前台 App 处理。

多个操作示例：
{"actions":[{"action":"open_app","app":"tetris"},{"action":"respond_text","text":"已打开"}]}

注意：JSON 必须合法，不能包含多余文字。请通过respond_text来进行回复文字内容。

{app_context}
)";

// ============================================================
//  System Info 采集
// ============================================================

static std::string collectSystemInfo()
{
	std::string info;
	int batPct = BatteryManager::instance().getPercent();
	if (batPct >= 0) {
		info += "- 电池电量: " + std::to_string(batPct) + "%";
		info += BatteryManager::instance().isCharging() ? " (充电中)" : "";
		info += "\n";
	}
	if (wifiIsConnect()) {
		esp_ip4_addr_t ip = wifiStationGetIp();
		char ipStr[16];
		snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&ip));
		info += "- IP 地址: " + std::string(ipStr) + "\n";
		info += "- WiFi: 已连接\n";
	}
	else {
		info += "- WiFi: 未连接\n";
	}
	info += "- 屏幕分辨率: 720×1280\n";
	info += "- 型号: ESP32-P4\n";
	return info;
}

// ============================================================
//  单例
// ============================================================

AiAgentOverlay& AiAgentOverlay::instance()
{
	static AiAgentOverlay s_instance;
	return s_instance;
}

// ============================================================
//  start — 初始化（在 main.cpp 中调用）
// ============================================================

bool AiAgentOverlay::start(Display* display)
{
	if (m_started) return true;
	m_display = display;

	// 加载 AI 配置
	m_config = aiConfigLoad();

	auto guard = display->lockGuard();
	if (!guard) {
		ESP_LOGE(TAG, "无法锁定显示");
		return false;
	}

	buildUi();

	ESP_LOGI(TAG, "started (valid=%d, api=%s, model=%s)",
		m_config.valid, m_config.apiUrl.c_str(), m_config.model.c_str());

	m_started = true;
	return true;
}

// ============================================================
//  UI 构建
// ============================================================

void AiAgentOverlay::buildUi()
{
	constexpr int W = 680;  // 720 - 40px margin
	constexpr int H = 500;
	constexpr int PAD = 10;

	auto* smlFont = FontLoader::getDefault(FontLoader::FontSize::Small);   // 24px
	auto* medFont = FontLoader::getDefault(FontLoader::FontSize::Default);  // 32px

	lv_obj_t* sysLayer = lv_layer_sys();

	// ── 启动 LVGL 定时器手势检测（每 30ms，不拦截触摸事件） ──
	m_gestureTimer = lv_timer_create(gestureTimerCb, 30, this);

	// ── 创建手柄摇杆输入设备（LV_INDEV_TYPE_KEYPAD） ──
	m_gamepadIndev = lv_indev_create();
	lv_indev_set_type(m_gamepadIndev, LV_INDEV_TYPE_KEYPAD);
	lv_indev_set_read_cb(m_gamepadIndev, gamepadReadCb);
	lv_indev_set_user_data(m_gamepadIndev, this);
	m_gamepadGroup = lv_group_create();
	lv_indev_set_group(m_gamepadIndev, m_gamepadGroup);
	// 初始清空上一帧按键状态
	for (auto& b : m_prevButtons) b = 0;

	// ── 浮动窗口容器（建在 lv_layer_sys 上） ──
	m_container = lv_obj_create(sysLayer);
	lv_obj_set_size(m_container, W, H);
	lv_obj_set_pos(m_container, (720 - W) / 2, 30);
	lv_obj_set_style_bg_color(m_container, LV_COLOR_MAKE(0x22, 0x22, 0x32), 0);
	lv_obj_set_style_bg_opa(m_container, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(m_container, 12, 0);
	lv_obj_set_style_border_width(m_container, 1, 0);
	lv_obj_set_style_border_color(m_container, LV_COLOR_MAKE(0x44, 0x44, 0x55), 0);
	lv_obj_set_style_shadow_width(m_container, 20, 0);
	lv_obj_set_style_shadow_color(m_container, LV_COLOR_MAKE(0x00, 0x00, 0x00), 0);
	lv_obj_set_style_shadow_opa(m_container, LV_OPA_60, 0);
	lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollbar_mode(m_container, LV_SCROLLBAR_MODE_OFF);

	// 手动拖拽（始终操作 m_container，不依赖事件目标，避免标题栏干扰）
	lv_obj_add_flag(m_container, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_container, [](lv_event_t* e) {
		auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
		static lv_point_t startPos;
		static lv_point_t startPt;
		switch (lv_event_get_code(e)) {
		case LV_EVENT_PRESSED:
		{
			startPos.x = lv_obj_get_x(self->m_container);
			startPos.y = lv_obj_get_y(self->m_container);
			lv_indev_get_point(lv_indev_active(), &startPt);
			break;
		}
		case LV_EVENT_PRESSING:
		{
			lv_point_t curPt;
			lv_indev_get_point(lv_indev_active(), &curPt);
			int32_t dx = curPt.x - startPt.x;
			int32_t dy = curPt.y - startPt.y;
			lv_obj_set_pos(self->m_container, startPos.x + dx, startPos.y + dy);
			break;
		}
		case LV_EVENT_RELEASED:
			break;
		default:
			break;
		}
		}, LV_EVENT_ALL, this);
	lv_obj_add_event_cb(m_container, onDragEndCb, LV_EVENT_RELEASED, this);

	// ── 标题栏 ──
	m_titleBar = lv_obj_create(m_container);
	lv_obj_set_size(m_titleBar, lv_pct(100), 44);
	lv_obj_set_style_bg_color(m_titleBar, LV_COLOR_MAKE(0x2a, 0x2a, 0x3a), 0);
	lv_obj_set_style_bg_opa(m_titleBar, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_titleBar, 0, 0);
	lv_obj_set_style_radius(m_titleBar, 12, 0);
	lv_obj_set_style_radius(m_titleBar, 0, LV_STATE_DEFAULT);
	lv_obj_set_flex_flow(m_titleBar, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_left(m_titleBar, PAD, 0);
	lv_obj_set_style_pad_right(m_titleBar, PAD, 0);
	lv_obj_set_style_pad_top(m_titleBar, 4, 0);
	lv_obj_set_scrollbar_mode(m_titleBar, LV_SCROLLBAR_MODE_OFF);

	// 关闭按钮
	m_closeBtn = lv_btn_create(m_titleBar);
	lv_obj_set_size(m_closeBtn, 36, 36);
	lv_obj_set_style_bg_color(m_closeBtn, LV_COLOR_MAKE(0x44, 0x22, 0x22), 0);
	lv_obj_set_style_radius(m_closeBtn, 18, 0);
	lv_obj_add_event_cb(m_closeBtn, onCloseBtnCb, LV_EVENT_CLICKED, this);
	// 关闭按钮不从游戏手柄导航（关闭用 B 键）
	lv_group_remove_obj(m_closeBtn);
	lv_obj_t* closeLabel = lv_label_create(m_closeBtn);
	lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
	lv_obj_set_style_text_font(closeLabel, smlFont, 0);
	lv_obj_center(closeLabel);

	// 标题文字
	auto* title = lv_label_create(m_titleBar);
	lv_label_set_text(title, "AI 助手");
	lv_obj_set_style_text_font(title, medFont, 0);
	lv_obj_set_style_text_color(title, GUI::Color::TEXT, 0);
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// 标题栏不应拦截拖动事件：清除点击标志，让事件穿透到容器
	lv_obj_clear_flag(m_titleBar, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(m_titleBar, LV_OBJ_FLAG_CLICK_FOCUSABLE);

	// ── 对话记录（lv_label + 滚动容器，无光标 → 无自动滚动） ──
	m_chatWrapper = lv_obj_create(m_container);
	lv_obj_set_width(m_chatWrapper, lv_pct(100));
	lv_obj_set_flex_grow(m_chatWrapper, 1);
	lv_obj_set_style_border_width(m_chatWrapper, 0, 0);
	lv_obj_set_style_bg_color(m_chatWrapper, LV_COLOR_MAKE(0x2d, 0x2d, 0x3d), 0);
	lv_obj_set_style_bg_opa(m_chatWrapper, LV_OPA_30, 0);
	lv_obj_set_style_pad_all(m_chatWrapper, PAD, 0);
	lv_obj_set_style_radius(m_chatWrapper, 0, 0);
	lv_obj_set_scrollbar_mode(m_chatWrapper, LV_SCROLLBAR_MODE_OFF);
	lv_obj_add_flag(m_chatWrapper, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(m_chatWrapper, LV_OBJ_FLAG_SCROLL_ELASTIC);

	m_chatLog = lv_label_create(m_chatWrapper);
	lv_obj_set_width(m_chatLog, lv_pct(100));
	lv_label_set_text(m_chatLog, "");
	lv_obj_set_style_text_color(m_chatLog, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_chatLog, smlFont, 0);

	// 点击聊天区收起键盘，获得更大消息显示区域
	lv_obj_add_event_cb(m_chatWrapper, [](lv_event_t* e) {
		auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
		if (!lv_obj_has_flag(self->m_keyboard, LV_OBJ_FLAG_HIDDEN)) {
			self->setKeyboardVisible(false);
		}
		}, LV_EVENT_CLICKED, this);

	// ── 输入行 ──
	auto* inputRow = lv_obj_create(m_container);
	lv_obj_set_size(inputRow, lv_pct(100), 56);
	lv_obj_set_style_border_width(inputRow, 0, 0);
	lv_obj_set_style_bg_opa(inputRow, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_flow(inputRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_left(inputRow, 6, 0);
	lv_obj_set_style_pad_right(inputRow, 6, 0);
	lv_obj_set_style_pad_top(inputRow, 4, 0);
	lv_obj_set_style_pad_bottom(inputRow, 4, 0);
	lv_obj_set_scrollbar_mode(inputRow, LV_SCROLLBAR_MODE_OFF);

	m_inputField = lv_textarea_create(inputRow);
	lv_obj_set_flex_grow(m_inputField, 1);
	lv_obj_set_height(m_inputField, 46);
	lv_textarea_set_one_line(m_inputField, true);
	lv_obj_set_scrollbar_mode(m_inputField, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(m_inputField, LV_OBJ_FLAG_SCROLLABLE);
	lv_textarea_set_placeholder_text(m_inputField, "说一句话…");
	// 输入框不从游戏手柄导航（内容通过键盘按键输入）
	lv_group_remove_obj(m_inputField);
	lv_obj_set_style_radius(m_inputField, 8, 0);
	// 点击输入框展开键盘
	lv_obj_add_event_cb(m_inputField, [](lv_event_t* e) {
		auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
		if (lv_obj_has_flag(self->m_keyboard, LV_OBJ_FLAG_HIDDEN)) {
			self->setKeyboardVisible(true);
		}
		}, LV_EVENT_CLICKED, this);
	lv_obj_set_style_bg_color(m_inputField, LV_COLOR_MAKE(0x2d, 0x2d, 0x3d), 0);
	lv_obj_set_style_bg_opa(m_inputField, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_inputField, 1, 0);
	lv_obj_set_style_border_color(m_inputField, LV_COLOR_MAKE(0x44, 0x44, 0x55), 0);
	// ★ 字体 + 白色文字
	lv_obj_set_style_text_font(m_inputField, smlFont, 0);
	lv_obj_set_style_text_color(m_inputField, GUI::Color::TEXT, 0);

	m_sendBtn = lv_btn_create(inputRow);
	lv_obj_set_size(m_sendBtn, 48, 46);
	lv_obj_set_style_radius(m_sendBtn, 8, 0);
	lv_obj_set_style_bg_color(m_sendBtn, GUI::Color::PRIMARY, 0);
	lv_obj_add_event_cb(m_sendBtn, onSendBtnCb, LV_EVENT_CLICKED, this);
	// 发送按钮不从游戏手柄导航（发送用 START 键）
	lv_group_remove_obj(m_sendBtn);
	auto* sendLabel = lv_label_create(m_sendBtn);
	lv_label_set_text(sendLabel, "▶");
	lv_obj_set_style_text_font(sendLabel, smlFont, 0);
	lv_obj_center(sendLabel);

	// ── 键盘（调大按键：增加高度 + 缩小间隙） ──
	m_keyboard = lv_keyboard_create(m_container);
	lv_obj_set_width(m_keyboard, lv_pct(100));
	lv_obj_set_height(m_keyboard, 220);
	lv_keyboard_set_textarea(m_keyboard, m_inputField);
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x1a, 0x1a, 0x2e), 0);
	lv_obj_set_style_pad_row(m_keyboard, 3, 0);
	lv_obj_set_style_pad_column(m_keyboard, 3, 0);
	lv_group_add_obj(m_gamepadGroup, m_keyboard);
	// 键盘按键焦点高亮（btnmatrix 按键是 LV_PART_ITEMS，不是主部件）
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_opa(m_keyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_text_color(m_keyboard, LV_COLOR_MAKE(0xff, 0xff, 0xff), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(m_keyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_keyboard, onKeyboardReadyCb, LV_EVENT_READY, this);
	// 设键盘为默认焦点
	lv_group_focus_obj(m_keyboard);
	// 调试：输出键盘状态确认焦点是否生效
	ESP_LOGI(TAG, "键盘焦点已设置，group=%p obj=%p", (void*)m_gamepadGroup, (void*)m_keyboard);

	// ── 初始隐藏 ──
	lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
//  show / hide / toggle
// ============================================================

void AiAgentOverlay::show()
{
	if (m_visible) return;
	if (!m_started || !m_container) return;

	m_visible = true;
	lv_obj_clear_flag(m_container, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);

	lv_textarea_set_cursor_pos(m_inputField, LV_TEXTAREA_CURSOR_LAST);
	lv_keyboard_set_textarea(m_keyboard, m_inputField);

	updateContextHint();
	ESP_LOGI(TAG, "show");
}

void AiAgentOverlay::hide()
{
	if (!m_visible) return;

	m_visible = false;

	// 断开键盘 → 释放输入焦点
	lv_keyboard_set_textarea(m_keyboard, nullptr);
	lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_container, LV_OBJ_FLAG_HIDDEN);

	// 重置等待状态
	m_isWaiting = false;

	ESP_LOGI(TAG, "hide");
}

void AiAgentOverlay::toggle()
{
	if (m_visible)
		hide();
	else
		show();
}

void AiAgentOverlay::requestShow()
{
	Task::addTask([](void* param) -> TickType_t {
		auto* self = static_cast<AiAgentOverlay*>(param);
		if (auto g = self->m_display->lockGuard()) {
			self->show();
		}
		return Task::infinityTime;
		}, "ovShow", this, 10, Task::Affinity::None);
}

void AiAgentOverlay::requestToggle()
{
	Task::addTask([](void* param) -> TickType_t {
		auto* self = static_cast<AiAgentOverlay*>(param);
		if (auto g = self->m_display->lockGuard()) {
			self->toggle();
		}
		return Task::infinityTime;
		}, "ovToggle", this, 10, Task::Affinity::None);
}

void AiAgentOverlay::setKeyboardVisible(bool visible)
{
	if (visible) {
		lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
		lv_keyboard_set_textarea(m_keyboard, m_inputField);
	}
	else {
		lv_keyboard_set_textarea(m_keyboard, nullptr);
		lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
	}
}

// ============================================================
//  对话记录管理
// ============================================================

void AiAgentOverlay::addChatEntry(const std::string& role, const std::string& text)
{
	std::string entry = "[" + role + "] " + text + "\n";
	m_chatHistory.push_back(entry);

	// 只追加新条目，不重建全文（lv_label_set_text 每次会更新整个文本）
	// 但为了保持 m_chatHistory 的完整性，仍从 history 重建
	std::string full;
	for (const auto& line : m_chatHistory) {
		full += line;
	}
	auto guard = m_display->lockGuard();
	lv_label_set_text(m_chatLog, full.c_str());
	// lv_label 无光标，不会自动滚动
}

void AiAgentOverlay::setLoading(bool loading)
{
	m_isWaiting = loading;
	// 从历史重建文本，追加/移除 "[思考中...]" 到末尾，随消息列表一起滚动
	std::string full;
	for (const auto& line : m_chatHistory) {
		full += line;
	}
	if (loading) {
		full += "[ 思考中... ]\n";
	}
	auto guard = m_display->lockGuard();
	lv_label_set_text(m_chatLog, full.c_str());
}

void AiAgentOverlay::updateContextHint()
{
	// ★ 核心改进：overlay 不占栈，getActiveApp() 返回底层真实 App
	auto* active = m_display->getActiveApp();
	if (active) {
		ESP_LOGI(TAG, "底层 App: %p, 获取上下文", (void*)active);
	}
}

// ============================================================
//  发送逻辑 — onSend
// ============================================================

void AiAgentOverlay::onSend()
{
	if (m_isWaiting) return;

	// 防重
	if (xTaskGetTickCount() < m_nextActionTime) return;
	m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN;

	// 获取输入
	const char* inputPtr{};
	{
		auto guard = m_display->lockGuard();
		inputPtr = lv_textarea_get_text(m_inputField);
	}
	if (!inputPtr || inputPtr[0] == '\0') return;
	std::string input(inputPtr);

	ESP_LOGI(TAG, "用户输入: %s", input.c_str());

	// 断开键盘 → 清空输入框 → 移除焦点
	{
		auto guard = m_display->lockGuard();
		lv_keyboard_set_textarea(m_keyboard, nullptr);
		lv_textarea_set_text(m_inputField, "");
		lv_obj_clear_state(m_inputField, LV_STATE_FOCUSED);
	}

	// 加入对话记录
	addChatEntry(">>", input);
	m_lastUserInput = input;

	// 检查配置
	if (!m_config.valid) {
		addChatEntry("!", "AI 未配置，请先设置 API Key");
		return;
	}

	setLoading(true);

	// 获取前台 App 上下文
	std::string appContext;
	auto* active = m_display->getActiveApp();
	if (active) {
		appContext = active->getAiContext();
		ESP_LOGI(TAG, "前台 App 上下文: %s", appContext.c_str());
	}

	// 读取 system prompt
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
			systemPrompt.replace(pos, 13,
				appContext.empty() ? "当前 App 无自定义操作" : appContext);
		}
		else if (!appContext.empty()) {
			systemPrompt += "\n\n当前应用上下文：\n" + appContext;
		}
	}

	m_lastSystemPrompt = systemPrompt;

	// 发送请求
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

// ============================================================
//  发送逻辑 — onAiResponse
// ============================================================

void AiAgentOverlay::onAiResponse(bool success, const std::string& responseText, const std::string& errorMsg)
{
	auto guard = m_display->lockGuard();

	ESP_LOGI(TAG, "onAiResponse: success=%d, textLen=%zu, err=%s",
		success, responseText.size(), errorMsg.c_str());

	setLoading(false);

	// 重新连接键盘
	lv_keyboard_set_textarea(m_keyboard, m_inputField);

	if (!success) {
		addChatEntry("!", "请求失败: " + errorMsg);
		return;
	}

	// 解析
	AiResponse response = AiParser::parse(responseText);

	if (!response.success && response.actions.empty()) {
		addChatEntry("!", "解析失败: " + response.errorMsg);
		if (!response.rawContent.empty()) {
			std::string raw = response.rawContent;
			if (raw.size() > 500) raw = raw.substr(0, 500) + "...";
			addChatEntry("RAW", raw);
		}
		return;
	}

	// ── 1. 提取 respond_text ──
	std::string replyText;
	for (const auto& action : response.actions) {
		if (action.type == AiActionType::RespondText && !action.text.empty()) {
			if (!replyText.empty()) replyText += "\n";
			replyText += action.text;
		}
		else if (action.type == AiActionType::ParseError) {
			if (!replyText.empty()) replyText += "\n";
			replyText += "[!] " + action.text;
		}
	}
	if (!replyText.empty()) {
		addChatEntry("AI", replyText);
	}

	m_lastAiContent = response.rawContent;

	// ── 2. 执行系统动作 ──
	auto* mgr = m_display->getStackManager();
	auto execResult = AiExecutor::execute(response.actions, m_display, mgr);
	if (replyText.empty() && execResult.totalCount > 0) {
		addChatEntry("AI", execResult.summary);
	}

	// ── 3. Agent 循环 ──
	if (m_followUpCount < MAX_FOLLOW_UP) {
		std::string observations;
		for (const auto& action : response.actions) {
			if (action.type == AiActionType::SystemInfo) {
				observations += collectSystemInfo();
			}
		}

		if (!observations.empty()) {
			m_followUpCount++;
			ESP_LOGI(TAG, "Agent 第 %d 轮", m_followUpCount);
			addChatEntry(">>", "(采集到数据，AI 正在分析...)");
			setLoading(true);

			std::vector<std::pair<std::string, std::string>> history;
			history.push_back({ "user", m_lastUserInput });
			{
				cJSON* root = cJSON_Parse(m_lastAiContent.c_str());
				if (root) {
					cJSON* choices = cJSON_GetObjectItem(root, "choices");
					if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
						cJSON* msg = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
						cJSON* content = cJSON_GetObjectItem(msg, "content");
						if (cJSON_IsString(content) && content->valuestring) {
							history.push_back({ "assistant", content->valuestring });
						}
					}
					cJSON_Delete(root);
				}
			}

			HttpClient::sendWithHistory(m_config, m_lastSystemPrompt,
				"[工具返回数据]\n" + observations + "\n请根据以上内容继续或向用户进行总结。",
				history,
				[this](bool ok, const std::string& resp, const std::string& err) {
					auto* ctx = new AiSendCtx{ this, resp, err, ok };
					Task::addTask([](void* p) -> TickType_t {
						auto* c = static_cast<AiSendCtx*>(p);
						c->self->onAiResponse(c->ok, c->text, c->err);
						delete c;
						return Task::infinityTime;
						}, "aiFup", ctx, 0, Task::Affinity::None);
				});
			return;
		}
	}

	m_followUpCount = 0;
}

// ============================================================
//  LVGL 定时器手势检测（在 LVGL 工作线程内运行，不拦截触摸事件）
// ============================================================
//  手柄摇杆输入回调（LV_INDEV_TYPE_KEYPAD）
// ============================================================

void AiAgentOverlay::gamepadReadCb(lv_indev_t* indev, lv_indev_data_t* data)
{
	auto* self = static_cast<AiAgentOverlay*>(lv_indev_get_user_data(indev));
	if (!self || !self->m_started) {
		data->key = 0;
		data->state = LV_INDEV_STATE_RELEASED;
		return;
	}

	// 默认无按键
	data->key = 0;
	data->state = LV_INDEV_STATE_RELEASED;

	// 找到第一个已连接的手柄
	const GamepadState* gs = nullptr;
	uint8_t gsPlayer = 0;
	for (uint8_t p = 0; p < MaxPlayers; p++) {
		const auto* dev = BleGamepad::instance().getDevice(p);
		if (dev && dev->connected) {
			gs = &dev->state;
			gsPlayer = p;
			break;
		}
	}
	if (!gs) return;

	uint16_t pressed = gs->buttons;
	uint16_t prev = self->m_prevButtons[gsPlayer];
	uint16_t edge = pressed & ~prev;  // 仅刚按下的键
	self->m_prevButtons[gsPlayer] = pressed;

	// ── 按钮（仅边沿触发，优先于方向映射，键盘隐藏时也能响应） ──
	if (edge & static_cast<uint16_t>(GamepadButton::BTN_X)) {
		// X 键切换键盘
		Task::addTask([](void* p) -> TickType_t {
			auto* s = static_cast<AiAgentOverlay*>(p);
			if (auto g = s->m_display->lockGuard()) {
				s->setKeyboardVisible(lv_obj_has_flag(s->m_keyboard, LV_OBJ_FLAG_HIDDEN));
			}
			return Task::infinityTime;
		}, "gpKbd", self, 0, Task::Affinity::None);
		return;
	}
	if (edge & static_cast<uint16_t>(GamepadButton::BTN_B)) {
		data->key = LV_KEY_ESC;
		data->state = LV_INDEV_STATE_PRESSED;
		return;
	}
	if (edge & static_cast<uint16_t>(GamepadButton::BTN_A) ||
		edge & static_cast<uint16_t>(GamepadButton::BTN_L3)) {
		data->key = LV_KEY_ENTER;
		data->state = LV_INDEV_STATE_PRESSED;
		return;
	}
	if (edge & static_cast<uint16_t>(GamepadButton::BTN_Y)) {
		// Y 发送消息
		Task::addTask([](void* p) -> TickType_t {
			static_cast<AiAgentOverlay*>(p)->onSend();
			return Task::infinityTime;
		}, "gpSend", self, 0, Task::Affinity::None);
		return;
	}

	// ── 方向键映射 ──
	{
		bool kbdHidden = lv_obj_has_flag(self->m_keyboard, LV_OBJ_FLAG_HIDDEN);
		constexpr uint8_t deadZone = 13;
		constexpr uint8_t center = 128;

		if (kbdHidden) {
			// 键盘隐藏 → 摇杆上下滚动聊天区
			if (gs->ly < center - deadZone) {
				lv_obj_scroll_by_bounded(self->m_chatWrapper, 0, +20, LV_ANIM_OFF);
			}
			else if (gs->ly > center + deadZone) {
				lv_obj_scroll_by_bounded(self->m_chatWrapper, 0, -20, LV_ANIM_OFF);
			}
			return;
		}

		// 键盘显示 → 摇杆导航键盘按键
		if (gs->lx < center - deadZone) {
			data->key = LV_KEY_LEFT;
			data->state = LV_INDEV_STATE_PRESSED;
			return;
		}
		if (gs->lx > center + deadZone) {
			data->key = LV_KEY_RIGHT;
			data->state = LV_INDEV_STATE_PRESSED;
			return;
		}
		if (gs->ly < center - deadZone) {
			data->key = LV_KEY_UP;
			data->state = LV_INDEV_STATE_PRESSED;
			return;
		}
		if (gs->ly > center + deadZone) {
			data->key = LV_KEY_DOWN;
			data->state = LV_INDEV_STATE_PRESSED;
			return;
		}
	}
}

// ============================================================

void AiAgentOverlay::gestureTimerCb(lv_timer_t* timer)
{
	auto* self = static_cast<AiAgentOverlay*>(lv_timer_get_user_data(timer));
	if (!self || !self->m_started) return;

	// ── 1. 手柄组合键检测 ──
	//     （放在最前面，overlay 显示时也能触发关闭）
	{
		bool comboNow = false;
		for (uint8_t p = 0; p < MaxPlayers; p++) {
			const auto* dev = BleGamepad::instance().getDevice(p);
			if (dev && dev->connected &&
				dev->state.isPressed(BTN_Y) &&
				dev->state.isPressed(BTN_X) &&
				dev->state.isPressed(BTN_B)) {
				comboNow = true;
				break;
			}
		}
		// 边沿检测：只有从 false→true 才触发，防止按住时重复触发
		if (comboNow && !self->m_gamepadComboPrev) {
			ESP_LOGI(TAG, "组合键触发 overlay");
			self->requestToggle();
		}
		self->m_gamepadComboPrev = comboNow;
	}

	// ── 2. 下滑手势检测 ──
	//     overlay 显示时不检测触摸（防遮挡干扰）
	if (self->m_visible) return;

	//     扫描所有 indev，找到第一个处于 PRESSED 状态的 POINTER
	lv_indev_t* indev = nullptr;
	lv_indev_t* activeIndev = nullptr;
	while ((indev = lv_indev_get_next(indev))) {
		if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) continue;
		if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
			activeIndev = indev;
			break;
		}
	}
	if (!activeIndev) {
		// 没有任何触摸按下
		if (self->m_gestureTracking) {
			ESP_LOGI(TAG, "[手势] 触摸结束");
		}
		self->m_gestureTracking = false;
		return;
	}

	// ── 有触摸按下 ──
	lv_point_t pt;
	lv_indev_get_point(activeIndev, &pt);

	if (!self->m_gestureTracking) {
		// 只跟踪顶部区域（Y < 30）的触摸
		if (pt.y < 30) {
			self->m_gestureStart = pt;
			self->m_gestureTracking = true;
			ESP_LOGI(TAG, "[手势] 开始跟踪顶部触摸: start=(%d,%d)", pt.x, pt.y);
		}
	}
	else {
		int32_t dy = pt.y - self->m_gestureStart.y;
		int32_t dx = pt.x - self->m_gestureStart.x;

		if (dy > SWIPE_THRESHOLD && std::abs(dx) < dy) {
			ESP_LOGI(TAG, "✅ 下滑手势触发 overlay (dy=%ld)", (long)dy);
			self->m_gestureTracking = false;
			self->requestShow();
		}
	}
}

// ============================================================
//  LVGL 事件回调
// ============================================================

void AiAgentOverlay::onCloseBtnCb(lv_event_t* e)
{
	auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
	self->hide();
}

void AiAgentOverlay::onSendBtnCb(lv_event_t* e)
{
	auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
	self->onSend();
}

void AiAgentOverlay::onKeyboardReadyCb(lv_event_t* e)
{
	auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
	self->onSend();
}

void AiAgentOverlay::onDragEndCb(lv_event_t* e)
{
	// self 仅用于 TAG 日志，这里不使用以避免 warning
	// auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
	auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));

	// 获取屏幕尺寸和容器尺寸
	lv_display_t* disp = lv_obj_get_disp(obj);
	int32_t screenW = lv_display_get_horizontal_resolution(disp);
	int32_t screenH = lv_display_get_vertical_resolution(disp);
	int32_t objW = lv_obj_get_width(obj);
	int32_t objH = lv_obj_get_height(obj);

	// 限制不超出屏幕边界
	int32_t x = lv_obj_get_x(obj);
	int32_t y = lv_obj_get_y(obj);
	x = std::max<int32_t>(0, std::min(x, screenW - objW));
	y = std::max<int32_t>(0, std::min(y, screenH - objH));
	lv_obj_set_pos(obj, x, y);
}
