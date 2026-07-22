#include "aiAgentOverlay.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "aiAgent/aiClient.hpp"
#include "aiAgent/aiParser.hpp"
#include "aiAgent/aiExecutor.hpp"
#include "aiAgent/aiSystemInfo.hpp"
#include "bleGamepad/bleGamepad.hpp"

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
请严格以 JSON 格式返回操作指令。

重要：必须始终使用 {"actions":[...]} 数组形式，即使只有一个操作也要放在数组中。系统会依次执行对应操作。

顶层可选字段（与 actions 同级）：
- memory:    长期记忆字符串，需要更新时写入
- nextRound: true/false，设为 true 时系统会执行下一轮并带上新的系统信息（如果有）

在 JSON 顶层添加 "nextRound": true 可以触发多轮操作。如果不需要继续，省略 nextRound 或设为 false 即可。

每个 action 对象的通用字段：
- action:   操作类型（必填）
- text:     字符串参数（app名、焦点组名、回复内容、文件路径等）
- value:    数值参数（亮度、音量、焦点索引等）
- params:   扩展参数字典（复杂结构用 JSON 对象）

可用系统操作：
- respond_text	回复文字	{"action":"respond_text","text":"你好"}
- open_app	打开应用	{"action":"open_app","text":"tetris"}
- navigate_back	返回上一级App	{"action":"navigate_back"}
- navigate_home	返回桌面	{"action":"navigate_home"}
- set_brightness	调亮度	{"action":"set_brightness","value":50}
- set_volume	调音量	{"action":"set_volume","value":70}
- play_audio	播放音频	{"action":"play_audio","text":"/path/file.mp3"}
- wifi_connect	连接WiFi	{"action":"wifi_connect","params":{"ssid":"...","password":"..."}}
- system_info	获取设备信息（需搭配nextRound）	{"action":"system_info"}
- power_off	关机	{"action":"power_off"}
- select	移动焦点到指定元素	{"action":"select","text":"groupName","value":2}
- activate	触发当前焦点的元素	{"action":"activate"}

示例——选中启动卡片并回复消息：
{"actions":[{"action":"select","text":"cards","value":2},{"action":"activate"},{"action":"respond_text","text":"已激活2号卡片"}]}

示例——聚焦到状态栏的0号元素并启动：
{"actions":[{"action":"select","text":"status","value":0},{"action":"activate"}]}

示例——单个回复操作：
{"actions":[{"action":"respond_text","text":"你好"}]}

示例——打开时间设置，并激活下一轮以在获取时间设置内的信息之后再进行后续操作（如修改时间）：
{"actions":[{"action":"navigate_home"},{"action":"open_app","text":"timeSettings"}],"nextRound":true}

不同的 app 中可用操作不一样，可用应用也不一样，在新 app 中无法使用旧 app 中的操作。有时需要先切换 app 再进行对应操作。

若用户请求的功能不在当前 app 中，也不在系统操作列表中，请尝试查找是否有相关 app（如电源管理、WiFi 设置等，desktop中会有大部分app的接口），然后切换到可能的app中并设置nextRound=true，在下一轮获取该 app 的界面信息后再继续。
例如：用户要求"重启"，系统没有直接的重启操作，但 powerManagement 应用内有重启按钮。
→ 先 {"actions":[{"action":"navigate_home"},{"action":"open_app","text":"powerManagement"}],"nextRound":true}
→ 下一轮会显示 powerManagement 的焦点组（如 buttons(0-1)=关机/重启），再进行 select+activate。

系统只保留最近 5 轮对话，超过的会被丢弃。
如果你认为某些信息在未来仍然重要，请主动将其写入 "memory" 字符串字段中进行长期记忆。

{memory_context}

{app_context}

注意：JSON 必须合法，不能包含多余文字，所有字段名和字符串值使用双引号。
)";



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

	// 中/EN 模式指示器（默认中文）
	m_modeLabel = lv_label_create(m_titleBar);
	lv_label_set_text(m_modeLabel, "中");
	lv_obj_set_style_text_font(m_modeLabel, smlFont, 0);
	lv_obj_set_style_text_color(m_modeLabel, GUI::Color::SUBTLE, 0);

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

	// ── 拼音 IME + 键盘 ──
	// 1) 先创建键盘（确保它在候选面板之前）
	m_keyboard = lv_keyboard_create(m_container);

	// 2) 创建 IME（构造时会创建候选面板，位于键盘之后）
	m_pinyinIme = lv_ime_pinyin_create(m_container);
	lv_obj_set_style_text_font(m_pinyinIme, smlFont, 0);  // 自动传播到候选面板

	// 3) 关联 IME ↔ 键盘
	lv_ime_pinyin_set_keyboard(m_pinyinIme, m_keyboard);
	lv_keyboard_set_textarea(m_keyboard, m_inputField);

	// 4) 获取候选面板，移到键盘前面（IME 构造时它在键盘之后）
	m_candPanel = lv_ime_pinyin_get_cand_panel(m_pinyinIme);
	lv_obj_move_to_index(m_candPanel, lv_obj_get_index(m_keyboard));
	lv_obj_set_height(m_candPanel, 44);
	lv_obj_set_width(m_candPanel, lv_pct(100));
	lv_obj_set_style_pad_row(m_candPanel, 4, 0);
	// IME 析构时会删除候选面板，我们将其移入 container 后 lv_ime_pinyin 解引时不会失效
	// 因为 lv_ime_pinyin 内部有有效性检查
	// 恢复 CLICK_FOCUSABLE（IME 构造时移除了它，游戏手柄导航需要）
	lv_obj_add_flag(m_candPanel, LV_OBJ_FLAG_CLICK_FOCUSABLE);
	// 候选面板加入游戏手柄组
	lv_group_add_obj(m_gamepadGroup, m_candPanel);
	// IME 对象隐藏且不参与布局
	lv_obj_add_flag(m_pinyinIme, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// 5) 候选面板选中候选字后延迟切焦点回键盘（翻页< >不切）
	lv_obj_add_event_cb(m_candPanel, [](lv_event_t* e) {
		if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
		auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
		if (!self->m_keyboard) return;
		// 跳过翻页按钮（idx=0:'<', idx=CAND_NUM+1:'>'）
		uint32_t btnId = lv_buttonmatrix_get_selected_button(self->m_candPanel);
		if (btnId == 0 || btnId == (LV_IME_PINYIN_CAND_TEXT_NUM + 1)) return;
		Task::addTask([](void* p) -> TickType_t {
			auto* s = static_cast<AiAgentOverlay*>(p);
			if (auto g = s->m_display->lockGuard()) {
				lv_group_focus_obj(s->m_keyboard);
			}
			return Task::infinityTime;
		}, "kbdFocus", self, 0, Task::Affinity::None);
		}, LV_EVENT_VALUE_CHANGED, this);

	// 6) 候选面板暗色主题样式
	lv_obj_set_style_bg_opa(m_candPanel, LV_OPA_30, 0);
	lv_obj_set_style_bg_color(m_candPanel, LV_COLOR_MAKE(0x2d, 0x2d, 0x3d), 0);
	lv_obj_set_style_bg_opa(m_candPanel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(m_candPanel, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_text_color(m_candPanel, GUI::Color::TEXT, LV_PART_ITEMS);
	lv_obj_set_style_text_color(m_candPanel, LV_COLOR_MAKE(0xff, 0xff, 0xff), LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_radius(m_candPanel, 6, LV_PART_ITEMS);
	lv_obj_set_style_border_width(m_candPanel, 0, 0);
	lv_obj_set_style_border_width(m_candPanel, 0, LV_PART_ITEMS);

	// 6) 键盘通用样式（使用默认键盘 map，不改动按钮布局）
	lv_obj_set_width(m_keyboard, lv_pct(100));
	lv_obj_set_height(m_keyboard, 180);  // 原 220px，让 40px 给候选面板
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x1a, 0x1a, 0x2e), 0);
	lv_obj_set_style_pad_row(m_keyboard, 3, 0);
	lv_obj_set_style_pad_column(m_keyboard, 3, 0);
	lv_group_add_obj(m_gamepadGroup, m_keyboard);
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_opa(m_keyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_text_color(m_keyboard, LV_COLOR_MAKE(0xff, 0xff, 0xff), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_color(m_keyboard, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(m_keyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);

	// 8) 拦截键盘图标按钮（LV_SYMBOL_KEYBOARD / LV_SYMBOL_CLOSE）作为中/EN 切换
	lv_obj_add_event_cb(m_keyboard, kbExtraBtnCb, LV_EVENT_VALUE_CHANGED, this);
	lv_obj_add_event_cb(m_keyboard, onKeyboardReadyCb, LV_EVENT_READY, this);

	// 9) 设键盘为默认焦点
	lv_group_focus_obj(m_keyboard);
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

	// 连接到键盘（IME 回调始终注册，只需重新关联 textarea）
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
		// 隐藏候选面板
		if (m_candPanel) lv_obj_add_flag(m_candPanel, LV_OBJ_FLAG_HIDDEN);
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

	// 保存原始模板（含占位符），供 agent 循环重建用
	m_systemPromptTemplate = systemPrompt;

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

	// 替换 {memory_context}
	{
		size_t pos = systemPrompt.find("{memory_context}");
		if (pos != std::string::npos) {
			systemPrompt.replace(pos, 16,
				m_memoryText.empty() ? "（暂无长期记忆）" : m_memoryText);
		}
		else if (!m_memoryText.empty()) {
			systemPrompt += "长期记忆：\n" + m_memoryText;
		}
	}

	m_lastSystemPrompt = systemPrompt;

	// 构建对话历史
	std::vector<std::pair<std::string, std::string>> history;
	history.reserve(m_history.size());
	for (const auto& [role, content] : m_history) {
		history.push_back({ role, content });
	}

	// 发送请求（带历史上下文）
	HttpClient::sendWithHistory(m_config, systemPrompt, input, history,
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

	// ── 3. Agent 循环（由 AI 的 nextRound 字段控制是否继续） ──
	if (response.nextRound && m_followUpCount < MAX_FOLLOW_UP) {
		std::string observations;
		for (const auto& action : response.actions) {
			if (action.type == AiActionType::SystemInfo) {
				observations += collectSystemInfo();
			}
		}

		m_followUpCount++;
		ESP_LOGI(TAG, "Agent 第 %d 轮", m_followUpCount);
		setLoading(true);

		// 从原始模板重建 system prompt，带上新 App 上下文
		{
			std::string newPrompt = m_systemPromptTemplate;
			std::string newCtx;
			auto* nextApp = m_display->getActiveApp();
			if (nextApp) newCtx = nextApp->getAiContext();

			// 替换 {app_context}
			size_t pos = newPrompt.find("{app_context}");
			if (pos != std::string::npos) {
				newPrompt.replace(pos, 13,
					newCtx.empty() ? "当前 App 无自定义操作" : newCtx);
			}

			// 替换 {memory_context}
			pos = newPrompt.find("{memory_context}");
			if (pos != std::string::npos) {
				newPrompt.replace(pos, 16,
					m_memoryText.empty() ? "（暂无长期记忆）" : m_memoryText);
			}

			m_lastSystemPrompt = newPrompt;
		}

		// 构建 follow-up 消息
		std::string followUpMsg;
		if (!observations.empty()) {
			followUpMsg += "[工具返回数据]\n" + observations + "\n\n请根据以上数据继续或向用户进行总结。";
			addChatEntry(">>", "(采集到数据，AI 正在分析...)");
		} else {
			followUpMsg += "已执行上一轮的指令，请继续完成用户的需求。如果已完成目标，请回复用户。";
			addChatEntry(">>", "(继续执行...)");
		}

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
			followUpMsg, history,
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

	m_followUpCount = 0;

	// ── 4. 更新长期记忆 ──
	if (!response.memory.empty()) {
		m_memoryText = response.memory;
		ESP_LOGI(TAG, "记忆已更新: %s", response.memory.c_str());
	}

	// ── 5. 记录对话历史 ──
	std::string replyForHistory = replyText;
	if (replyForHistory.empty()) {
		replyForHistory = execResult.summary;
	}
	if (!replyForHistory.empty() && !m_lastUserInput.empty()) {
		m_history.push_back({ "user", m_lastUserInput });
		m_history.push_back({ "assistant", replyForHistory });
		// 滚动裁剪：保留最近 MAX_HISTORY_TURNS 轮
		while (m_history.size() / 2 > MAX_HISTORY_TURNS) {
			m_history.erase(m_history.begin());
			m_history.erase(m_history.begin());
		}
		ESP_LOGI(TAG, "对话历史: %zu 轮", m_history.size() / 2);
	}
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

	// overlay 未显示时不处理任何手柄输入（防止干扰底层 App）
	if (!self->m_visible) return;

	// ── 遍历所有已连接手柄，合并输入 ──
	//     按键（边沿）任一手柄触发即生效；方向取第一个有输入的
	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;
	bool kbdHidden = lv_obj_has_flag(self->m_keyboard, LV_OBJ_FLAG_HIDDEN);
	bool handled = false;

	for (uint8_t p = 0; p < MaxPlayers && !handled; p++) {
		const auto* dev = BleGamepad::instance().getDevice(p);
		if (!dev || !dev->connected) continue;

		const auto& gs = dev->state;
		uint16_t pressed = gs.buttons;
		uint16_t prev = self->m_prevButtons[p];
		uint16_t edge = pressed & ~prev;
		self->m_prevButtons[p] = pressed;

		// ── 按钮（边沿触发，优先于方向） ──
		if (edge & static_cast<uint16_t>(GamepadButton::BTN_X)) {
			bool candVisible = self->m_candPanel &&
				!lv_obj_has_flag(self->m_candPanel, LV_OBJ_FLAG_HIDDEN);
			if (candVisible) {
				// 有候选字：在键盘和候选面板间切换焦点
				lv_obj_t* focused = lv_group_get_focused(self->m_gamepadGroup);
				if (focused == self->m_keyboard)
					lv_group_focus_obj(self->m_candPanel);
				else
					lv_group_focus_obj(self->m_keyboard);
			} else {
				// 无候选字：切换键盘显隐（原行为）
				Task::addTask([](void* arg) -> TickType_t {
					auto* s = static_cast<AiAgentOverlay*>(arg);
					if (auto g = s->m_display->lockGuard())
						s->setKeyboardVisible(lv_obj_has_flag(s->m_keyboard, LV_OBJ_FLAG_HIDDEN));
					return Task::infinityTime;
					}, "gpKbd", self, 0, Task::Affinity::None);
			}
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
			Task::addTask([](void* arg) -> TickType_t {
				static_cast<AiAgentOverlay*>(arg)->onSend();
				return Task::infinityTime;
				}, "gpSend", self, 0, Task::Affinity::None);
			return;
		}

		// ── 方向键 ──
		if (kbdHidden) {
			if (gs.ly < center - deadZone) {
				lv_obj_scroll_by_bounded(self->m_chatWrapper, 0, +20, LV_ANIM_OFF);
				handled = true;
			}
			else if (gs.ly > center + deadZone) {
				lv_obj_scroll_by_bounded(self->m_chatWrapper, 0, -20, LV_ANIM_OFF);
				handled = true;
			}
		}
		else {
			// 检查焦点是否在候选面板上
			bool candFocused = self->m_candPanel &&
				(lv_group_get_focused(self->m_gamepadGroup) == self->m_candPanel);

			if (candFocused) {
				// 候选面板：左右选字，上下翻页
				if ((gs.ly < center - deadZone || gs.ly > center + deadZone) &&
					xTaskGetTickCount() < self->m_nextPageFlipTime) {
					// 冷却中，跳过
				}
				else if (gs.ly < center - deadZone) {
					self->m_nextPageFlipTime = xTaskGetTickCount() + PAGE_FLIP_COOLDOWN;
					// 上 ← 上一页
					Task::addTask([](void* p) -> TickType_t {
						auto pair = static_cast<std::pair<AiAgentOverlay*, lv_obj_t*>*>(p);
						if (auto g = pair->first->m_display->lockGuard()) {
							uint32_t b = 0;
							lv_buttonmatrix_set_selected_button(pair->second, b);
							lv_obj_send_event(pair->second, LV_EVENT_VALUE_CHANGED, &b);
							// pinyin_page_proc 更新了 cand_str 但没重绘，强制刷新
							lv_obj_invalidate(pair->second);
						}
						delete pair;
						return Task::infinityTime;
					}, "pgUp", new std::pair<AiAgentOverlay*, lv_obj_t*>(self, self->m_candPanel), 0, Task::Affinity::None);
					handled = true;
				}
				else if (gs.ly > center + deadZone) {
					self->m_nextPageFlipTime = xTaskGetTickCount() + PAGE_FLIP_COOLDOWN;
					Task::addTask([](void* p) -> TickType_t {
						auto pair = static_cast<std::pair<AiAgentOverlay*, lv_obj_t*>*>(p);
						if (auto g = pair->first->m_display->lockGuard()) {
							uint32_t b = LV_IME_PINYIN_CAND_TEXT_NUM + 1;
							lv_buttonmatrix_set_selected_button(pair->second, b);
							lv_obj_send_event(pair->second, LV_EVENT_VALUE_CHANGED, &b);
							lv_obj_invalidate(pair->second);
						}
						delete pair;
						return Task::infinityTime;
					}, "pgDn", new std::pair<AiAgentOverlay*, lv_obj_t*>(self, self->m_candPanel), 0, Task::Affinity::None);
					handled = true;
				}
				else if (gs.lx < center - deadZone) {
					data->key = LV_KEY_LEFT;  data->state = LV_INDEV_STATE_PRESSED;
					handled = true;
				}
				else if (gs.lx > center + deadZone) {
					data->key = LV_KEY_RIGHT; data->state = LV_INDEV_STATE_PRESSED;
					handled = true;
				}
			}
			else {
				// 键盘：btnmatrix 内部导航
				if (gs.lx < center - deadZone) {
					data->key = LV_KEY_LEFT;  data->state = LV_INDEV_STATE_PRESSED;
					handled = true;
				}
				else if (gs.lx > center + deadZone) {
					data->key = LV_KEY_RIGHT; data->state = LV_INDEV_STATE_PRESSED;
					handled = true;
				}
				else if (gs.ly < center - deadZone) {
					data->key = LV_KEY_UP;    data->state = LV_INDEV_STATE_PRESSED;
					handled = true;
				}
				else if (gs.ly > center + deadZone) {
					data->key = LV_KEY_DOWN;  data->state = LV_INDEV_STATE_PRESSED;
					handled = true;
				}
			}
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
//  中英文切换
// ============================================================

void AiAgentOverlay::setChineseMode(bool enable)
{
	if (m_isChineseMode == enable) return;
	m_isChineseMode = enable;

	// 清理 IME 中间状态（候选面板 + 输入缓冲）
	lv_obj_add_flag(m_candPanel, LV_OBJ_FLAG_HIDDEN);
	// 触发键盘 OK 键来清空 IME 内部 ta_count 等状态
	// 断开再重连 textarea 让 IME 重置
	if (m_keyboard && m_inputField) {
		lv_keyboard_set_textarea(m_keyboard, nullptr);
		lv_keyboard_set_textarea(m_keyboard, m_inputField);
	}

	if (enable) {
		lv_label_set_text(m_modeLabel, "中");
		lv_obj_set_style_text_color(m_modeLabel, GUI::Color::SUBTLE, 0);
		ESP_LOGI(TAG, "切换到中文输入");
	} else {
		lv_label_set_text(m_modeLabel, "EN");
		lv_obj_set_style_text_color(m_modeLabel, GUI::Color::SUBTLE, 0);
		ESP_LOGI(TAG, "切换到英文输入");
	}
}

void AiAgentOverlay::kbExtraBtnCb(lv_event_t* e)
{
	if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

	auto* self = static_cast<AiAgentOverlay*>(lv_event_get_user_data(e));
	if (!self || !self->m_keyboard) return;

	uint32_t btnId = lv_buttonmatrix_get_selected_button(self->m_keyboard);
	if (btnId == LV_BUTTONMATRIX_BUTTON_NONE) return;

	const char* txt = lv_buttonmatrix_get_button_text(self->m_keyboard, btnId);
	if (!txt) return;

	// 检测键盘图标按钮（LV_SYMBOL_KEYBOARD / LV_SYMBOL_CLOSE）按下
	if (lv_strcmp(txt, LV_SYMBOL_KEYBOARD) == 0 || lv_strcmp(txt, LV_SYMBOL_CLOSE) == 0) {
		self->setChineseMode(!self->m_isChineseMode);
	}

	// 英文模式下：隐藏 IME 可能弹出的候选面板
	if (!self->m_isChineseMode) {
		lv_obj_add_flag(self->m_candPanel, LV_OBJ_FLAG_HIDDEN);
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
