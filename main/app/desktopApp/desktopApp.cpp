#include "desktopApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "app/fruitNinja/fruitNinjaRoom.hpp"
#include "app/testApp/testApp.hpp"
#include "app/bleSettingsApp/bleSettingsApp.hpp"
#include "app/wifiSettingsApp/wifiSettingsApp.hpp"
#include "app/timeSettingsApp/timeSettingsApp.hpp"
#include "app/powerManagementApp/powerManagementApp.hpp"
#include "app/consoleManager/consoleManagerApp.hpp"
#include "elfManager/elfManager.hpp"
#include "app/tetris/tetrisRoomApp.hpp"
#include "app/chineseChess/chineseChessRoom.hpp"
#include "app/douDiZhu/douDiZhuRoom.hpp"
#include "app/luaGameApp/luaGameApp.hpp"
#include "app/luaManagerApp/luaManagerApp.hpp"
#include "app/marketApp/marketApp.hpp"

#include "audio/Audio.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/font.hpp"
#include "display/ili9881c.hpp"
#include "battery/batteryManager.hpp"

// ════════════════════════════════════════════════════════════════
// 动态卡片数据（内置 + 系统；ELF 卡片由扫描任务渐进式插入最前）
// ════════════════════════════════════════════════════════════════

void DesktopApp::buildBuiltinCards()
{
	// 内置游戏（未迁移 ELF 的原生 App）→ 索引 0..3
	static const struct {
		const char* name;
		const char* desc;
		const char* icon;
	} kBuiltin[] = {
		{ "水果忍者", "快速切水果，享受爽快连击！", "F:system/desktop/fruitNinja.jpg" },
		{ "俄罗斯方块", "经典俄罗斯方块，消除行数得分！", "F:system/desktop/tetris.jpg" },
		{ "斗地主", "三人斗地主，比拼牌技！", "F:system/desktop/douDiZhu.jpg" },
		{ "中国象棋", "中国象棋，楚河汉界对弈！", "F:system/desktop/chess.jpg" },
	};
	for (int i = 0; i < BUILTIN_COUNT; i++)
	{
		GameCard c{};
		c.kind = CardKind::Builtin;
		snprintf(c.displayName, sizeof(c.displayName), "%s", kBuiltin[i].name);
		snprintf(c.desc, sizeof(c.desc), "%s", kBuiltin[i].desc);
		snprintf(c.icon, sizeof(c.icon), "%s", kBuiltin[i].icon);
		c.index = (int8_t)i;
		m_cards.push_back(c);
	}

	// 系统卡片 → 索引 0..2
	static const struct {
		const char* name;
		const char* desc;
		const char* icon;
	} kSystem[] = {
		{ "Lua 脚本管理", "浏览/运行/管理 Lua 游戏脚本，支持 AI 生成新游戏", "F:system/desktop/lua.jpg" },
		{ "游戏市场", "浏览并下载社区 Lua 游戏脚本，扩展游戏库！", "F:system/desktop/gameMarket.jpg" },
		{ "测试", "测试", "F:system/desktop/test.jpg" },
	};
	for (int i = 0; i < SYSTEM_COUNT; i++)
	{
		GameCard c{};
		c.kind = CardKind::System;
		snprintf(c.displayName, sizeof(c.displayName), "%s", kSystem[i].name);
		snprintf(c.desc, sizeof(c.desc), "%s", kSystem[i].desc);
		snprintf(c.icon, sizeof(c.icon), "%s", kSystem[i].icon);
		c.index = (int8_t)i;
		m_cards.push_back(c);
	}
}

DesktopApp::DesktopApp(Display* display)
	: App(display)
{
}

DesktopApp::~DesktopApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void DesktopApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}

	// 页面背景
	lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	buildUi();

	// 创建时间更新定时器（每 1 秒更新一次）
	m_timeTimer = lv_timer_create([](lv_timer_t* t)
		{
			auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
			time_t now = time(nullptr);
			struct tm timeinfo{};
			localtime_r(&now, &timeinfo);
			if (timeinfo.tm_year <= 70) return;
			if (timeinfo.tm_hour == self->showedTime.tm_hour &&
				timeinfo.tm_min == self->showedTime.tm_min)
				return;

			char buf[8]{};
			strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
			lv_label_set_text(self->m_timeLabel, buf);
		}, 1000, this);
	lv_timer_ready(m_timeTimer); // 首帧立即触发

	// 初始聚焦
	m_focusGroup = FOCUS_CARDS;
	m_focusCardsIdx = 0;
	applyFocus();

	m_batteryTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		self->updateBatteryIcon();
		}, 5000, this);
	updateBatteryIcon();

	// 后台渐进式扫描 /root/elf/*.so → 动态插入 ELF 游戏卡片
	startElfScan();

	ESP_LOGI(TAG, "桌面初始化完成");
}

void DesktopApp::deinit()
{
	m_elfScanStopped = true;   // 终止后台扫描（任务在下一次更新前退出）
	if (m_timeTimer)
	{
		lv_timer_del(m_timeTimer);
		m_timeTimer = nullptr;
	}
	if (m_batteryTimer)
	{
		lv_timer_del(m_batteryTimer);
		m_batteryTimer = nullptr;
	}
	App::deinit();
	ESP_LOGI(TAG, "桌面已释放");
}

void DesktopApp::onForeground()
{
	// 防抖重置
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
	m_prevButtons = 0xFFFF;

	// 激活时间更新
	lv_timer_ready(m_timeTimer);

	ESP_LOGI(TAG, "前台");
}

void DesktopApp::onBackground()
{
	ESP_LOGI(TAG, "后台");
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void DesktopApp::buildUi()
{
	// ── 主区域（flex 列布局，撑满屏幕） ──
	auto main_area = lv_obj_create(screen);
	lv_obj_set_size(main_area, lv_pct(100), lv_pct(100));
	lv_obj_set_layout(main_area, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(main_area, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_border_width(main_area, 0, 0);
	lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(main_area, 0, 0);

	// ── 顶部状态行 ──
	auto status_row = GUI::createFlex(main_area, LV_FLEX_FLOW_ROW,
		lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(status_row, 8, 0);
	lv_obj_set_style_pad_left(status_row, 16, 0);
	lv_obj_set_style_pad_right(status_row, 16, 0);
	lv_obj_set_style_border_width(status_row, 0, 0);
	lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);

	m_timeLabel = GUI::createLabel(status_row, "--:--");
	lv_obj_set_style_text_color(m_timeLabel, GUI::Color::TEXT, 0);
	lv_obj_add_flag(m_timeLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_timeLabel, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = STATUS_TIME;
		Task::addTask([](void* param) -> TickType_t
			{
				auto* app = static_cast<DesktopApp*>(param);
				if (app->getManager()) {
					auto* timeApp = new TimeSettingsApp(app->getDisplay());
					app->getManager()->pushToNewStack(timeApp);
				}
				return Task::infinityTime;
			}, "openTimeSettings", self, 0, Task::Affinity::NotAssigned);
		}, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_timeLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_timeLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_timeLabel, 2, LV_STATE_FOCUSED);

	// ── 弹性分隔符（flex_grow 参考系，分隔时钟与状态图标） ──
	m_statusSpacer = lv_obj_create(status_row);
	lv_obj_set_size(m_statusSpacer, 0, 0);
	lv_obj_set_flex_grow(m_statusSpacer, SpaceGrow);
	lv_obj_set_style_border_width(m_statusSpacer, 0, 0);
	lv_obj_set_style_bg_opa(m_statusSpacer, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(m_statusSpacer, LV_OBJ_FLAG_CLICKABLE);

	m_wifiLabel = GUI::createLabel(status_row, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_color(m_wifiLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_wifiLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_wifiLabel, 16, 0);
	lv_obj_add_flag(m_wifiLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_wifiLabel, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = STATUS_WIFI;
		/* LVGL 事件回调中持锁，栈操作须延后 */
		Task::addTask([](void* param) -> TickType_t
			{
				auto* app = static_cast<DesktopApp*>(param);
				if (app->getManager())
				{
					auto* wifiApp = new WifiSettingsApp(app->getDisplay());
					app->getManager()->pushToNewStack(wifiApp);
				}
				return Task::infinityTime;
			}, "openWifiSettings", self, 0, Task::Affinity::NotAssigned);
		}, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_wifiLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_wifiLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_wifiLabel, 2, LV_STATE_FOCUSED);

	m_bluetoothLabel = GUI::createLabel(status_row, LV_SYMBOL_BLUETOOTH);
	lv_obj_set_style_text_color(m_bluetoothLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_bluetoothLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_bluetoothLabel, 16, 0);
	lv_obj_add_flag(m_bluetoothLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_bluetoothLabel, onBluetoothLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_bluetoothLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_bluetoothLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_bluetoothLabel, 2, LV_STATE_FOCUSED);

	// 音量
	m_volumeLabel = GUI::createLabel(status_row, LV_SYMBOL_VOLUME_MAX);
	lv_obj_set_style_text_color(m_volumeLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_volumeLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_volumeLabel, 16, 0);
	lv_obj_add_flag(m_volumeLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_volumeLabel, onVolumeLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_volumeLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_volumeLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_volumeLabel, 2, LV_STATE_FOCUSED);

	// 亮度
	m_brightnessLabel = GUI::createLabel(status_row, LV_SYMBOL_EYE_OPEN);
	lv_obj_set_style_text_color(m_brightnessLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_brightnessLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_brightnessLabel, 16, 0);
	lv_obj_add_flag(m_brightnessLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_brightnessLabel, onBrightnessLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_brightnessLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_brightnessLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_brightnessLabel, 2, LV_STATE_FOCUSED);

	// 多主机管理
	m_consoleLabel = GUI::createLabel(status_row, LV_SYMBOL_DRIVE);
	lv_obj_set_style_text_color(m_consoleLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_consoleLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_consoleLabel, 16, 0);
	lv_obj_add_flag(m_consoleLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_consoleLabel, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = STATUS_CONSOLE;
		Task::addTask([](void* param) -> TickType_t
			{
				auto* app = static_cast<DesktopApp*>(param);
				if (app->getManager()) {
					auto* consoleApp = new ConsoleManagerApp(app->getDisplay());
					app->getManager()->pushToNewStack(consoleApp);
				}
				return Task::infinityTime;
			}, "openConsoleManager", self, 0, Task::Affinity::NotAssigned);
		}, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_consoleLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_consoleLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_consoleLabel, 2, LV_STATE_FOCUSED);

	m_batteryLabel = GUI::createLabel(status_row, LV_SYMBOL_BATTERY_FULL);
	lv_obj_set_style_text_font(m_batteryLabel, LV_FONT_DEFAULT, 0);
	lv_obj_add_flag(m_batteryLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_batteryLabel, onBatteryLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_batteryLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_batteryLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_batteryLabel, 2, LV_STATE_FOCUSED);

	// ── 页面标题（居中） ──
	auto title = GUI::createTitle(main_area, "游戏中心");
	lv_obj_set_width(title, lv_pct(100));
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_top(title, 20, 0);
	lv_obj_set_style_pad_bottom(title, 10, 0);

	// ── 卡片区域（flex_grow 填充中间） ──
	auto cards_section = GUI::createFlex(main_area, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(cards_section, 1);
	lv_obj_set_style_pad_all(cards_section, 10, 0);
	lv_obj_set_style_pad_bottom(cards_section, 140, 0);
	lv_obj_set_style_pad_column(cards_section, 8, 0);
	lv_obj_set_style_border_width(cards_section, 0, 0);
	lv_obj_set_style_bg_opa(cards_section, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(cards_section, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 左箭头（触控备选，不参与焦点导航）
	lv_obj_set_style_pad_left(cards_section, 32, 0);
	m_prevBtn = GUI::createButton(cards_section, "<", 52, 52);
	lv_obj_set_style_radius(m_prevBtn, 30, 0);
	lv_obj_add_event_cb(m_prevBtn, onPrevBtnCb, LV_EVENT_CLICKED, this);

	// 卡片行（可触摸横向滚动）
	m_cardsRow = GUI::createFlex(cards_section, LV_FLEX_FLOW_ROW, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(m_cardsRow, 1);
	lv_obj_set_style_pad_column(m_cardsRow, 16, 0);
	lv_obj_set_style_border_width(m_cardsRow, 0, 0);
	lv_obj_set_style_bg_opa(m_cardsRow, LV_OPA_TRANSP, 0);
	lv_obj_set_scrollbar_mode(m_cardsRow, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_flex_align(m_cardsRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 创建卡片（内置 + 系统；ELF 卡片由扫描任务渐进式插入）
	buildBuiltinCards();
	for (int i = 0; i < (int)m_cards.size(); i++)
		createCardObject(i);

	// 右箭头（触控备选，不参与焦点导航）
	lv_obj_set_style_pad_right(cards_section, 32, 0);
	m_nextBtn = GUI::createButton(cards_section, ">", 52, 52);
	lv_obj_set_style_radius(m_nextBtn, 30, 0);
	lv_obj_add_event_cb(m_nextBtn, onNextBtnCb, LV_EVENT_CLICKED, this);

	// ── 底部区域 ──
	// 左侧：游戏简介（弹性宽度，自动换行）
	m_descLabel = GUI::createLabel(screen, "");
	lv_obj_set_style_text_color(m_descLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_descLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_width(m_descLabel, lv_pct(50));
	lv_label_set_long_mode(m_descLabel, LV_LABEL_LONG_WRAP);
	lv_obj_align(m_descLabel, LV_ALIGN_BOTTOM_LEFT, 40, -80);

	// 右侧：开始按钮（固定右下角）
	m_startBtn = GUI::createButton(screen, "开始游戏", 160, 50);
	lv_obj_set_style_radius(m_startBtn, 12, 0);
	lv_obj_set_style_border_width(m_startBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_startBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_startBtn, onStartBtnCb, LV_EVENT_CLICKED, this);
	lv_obj_align(m_startBtn, LV_ALIGN_BOTTOM_RIGHT, -50, -50);

	// 已选择提示（在开始按钮上方，随文字长度动态调整水平位置）
	m_infoLabel = GUI::createLabel(screen, "");
	lv_obj_set_style_text_color(m_infoLabel, GUI::Color::TEXT, 0);
	lv_obj_set_width(m_infoLabel, lv_pct(40));
	lv_obj_set_style_text_align(m_infoLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_align_to(m_infoLabel, m_startBtn, LV_ALIGN_OUT_TOP_RIGHT, 0, -8);

	// 初始化卡片状态
	updateSelectionLabels();
	applyFocus();
}

// ════════════════════════════════════════════════════════════════
// 动态卡片对象
// ════════════════════════════════════════════════════════════════

lv_obj_t* DesktopApp::createCardObject(int idx)
{
	auto card = lv_obj_create(m_cardsRow);
	lv_obj_set_size(card, CARD_W, CARD_H);
	lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(card, 16, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_pad_all(card, 0, 0);
	lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_user_data(card, (void*)(uintptr_t)idx);

	// 聚焦样式 — 仅用彩色边框，无阴影
	lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(card, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_border_side(card, LV_BORDER_SIDE_FULL, LV_STATE_FOCUSED);

	// 图片
	auto img = lv_image_create(card);
	lv_image_set_src(img, m_cards[idx].icon);
	lv_obj_set_size(img, lv_pct(100), lv_pct(100));
	lv_obj_center(img);

	// 点击聚焦（user_data=卡片索引，插入/更新后统一刷新）
	lv_obj_add_event_cb(card, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		auto* c = static_cast<lv_obj_t*>(lv_event_get_target(e));
		self->m_focusGroup = FOCUS_CARDS;
		self->m_focusCardsIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(c);
		self->updateSelectionLabels();
		self->applyFocus();
		}, LV_EVENT_CLICKED, this);

	if (idx >= (int)m_cardObjs.size())
		m_cardObjs.resize(idx + 1, nullptr);
	m_cardObjs[idx] = card;
	return card;
}

// ════════════════════════════════════════════════════════════════
// ELF 插件扫描（后台渐进式：占位卡片 → 逐个 inspect → 元数据就绪）
// ════════════════════════════════════════════════════════════════

void DesktopApp::startElfScan()
{
	if (m_elfScanStarted) return;
	m_elfScanStarted = true;
	m_elfScanStopped = false;
	Task::addTask(elfScanTask, "elfScan", this, 0, Task::Affinity::None);
}

TickType_t DesktopApp::elfScanTask(void* param)
{
	auto* self = static_cast<DesktopApp*>(param);
	auto& em = ElfManager::instance();

	// 1. 快速：列出 /root/elf/*.so → 全部先插占位卡片（立即可见，无阻塞）
	char paths[MAX_ELF_CARDS][64];
	size_t n = em.listElfFiles(paths, MAX_ELF_CARDS);
	for (size_t i = 0; i < n; i++)
	{
		if (self->m_elfScanStopped) return Task::infinityTime;
		self->addElfCardPlaceholder(paths[i]);
	}

	// 2. 渐进：逐个 inspect（dlopen→读元数据→dlclose，~600ms/个，无 LVGL 锁）
	//    完成后持锁更新对应卡片（名称/描述/图标，按 path 定位——插入顺序与文件顺序相反）
	for (size_t i = 0; i < n; i++)
	{
		if (self->m_elfScanStopped) return Task::infinityTime;
		ElfManager::ScanEntry e;
		if (em.inspectModule(paths[i], &e))
			self->updateElfCard(paths[i], e);
	}

	self->m_elfScanStopped = true;
	ESP_LOGI(TAG, "ELF 扫描完成: %u 个插件", (unsigned)n);
	return Task::infinityTime;
}

void DesktopApp::addElfCardPlaceholder(const char* fullPath)
{
	// 占位名：文件 basename 去 .so 扩展名
	const char* base = strrchr(fullPath, '/');
	base = base ? base + 1 : fullPath;
	char name[32];
	snprintf(name, sizeof(name), "%s", base);
	size_t len = strlen(name);
	if (len > 3) name[len - 3] = '\0';   // 去 ".so"

	GameCard c{};
	c.kind = CardKind::Elf;
	snprintf(c.displayName, sizeof(c.displayName), "%s", name);
	snprintf(c.pluginName, sizeof(c.pluginName), "%s", name);
	snprintf(c.desc, sizeof(c.desc), "加载中...");
	snprintf(c.elfPath, sizeof(c.elfPath), "%s", fullPath);
	c.index = -1;

	auto guard = display->lockGuard();   // 数据 + LVGL 对象须在锁内
	m_cards.insert(m_cards.begin(), c);
	m_cardObjs.insert(m_cardObjs.begin(), nullptr);   // 同步插入（防覆盖旧卡片对象引用）
	createCardObject(0);
	lv_obj_move_to_index(m_cardObjs[0], 0);   // 移到行首（内置卡片之前）
	// 插入后所有旧卡片索引 +1，刷新 user_data
	for (int i = 0; i < (int)m_cardObjs.size(); i++)
		lv_obj_set_user_data(m_cardObjs[i], (void*)(uintptr_t)i);

	ESP_LOGI(TAG, "ELF 占位卡片: %s", fullPath);
}

void DesktopApp::updateElfCard(const char* elfPath, const ElfManager::ScanEntry& e)
{
	auto guard = display->lockGuard();

	// 按路径定位卡片（ELF 卡片 insert(begin)，与 listElfFiles 顺序相反）
	int elfIdx = -1;
	for (int i = 0; i < (int)m_cards.size(); i++)
	{
		if (m_cards[i].kind == CardKind::Elf && strcmp(m_cards[i].elfPath, elfPath) == 0)
		{
			elfIdx = i;
			break;
		}
	}
	if (elfIdx < 0)
		return;

	GameCard& c = m_cards[elfIdx];
	if (e.displayName[0])
		snprintf(c.displayName, sizeof(c.displayName), "%s", e.displayName);
	if (e.name[0])
		snprintf(c.pluginName, sizeof(c.pluginName), "%s", e.name);
	if (e.desc[0])
		snprintf(c.desc, sizeof(c.desc), "%s", e.desc);
	else
		snprintf(c.desc, sizeof(c.desc), "ELF 游戏插件");
	if (e.iconPath[0])
		snprintf(c.icon, sizeof(c.icon), "%s", e.iconPath);

	// 刷新卡片图标（图片是 card 的第一个子对象）
	if (elfIdx < (int)m_cardObjs.size() && m_cardObjs[elfIdx])
	{
		auto img = lv_obj_get_child(m_cardObjs[elfIdx], 0);
		if (img) lv_image_set_src(img, c.icon);
	}
	if (elfIdx == m_focusCardsIdx)
		updateSelectionLabels();
	ESP_LOGI(TAG, "ELF 元数据就绪: %s → %s", e.path, c.displayName);
}

// ════════════════════════════════════════════════════════════════
// 选中标签更新
// ════════════════════════════════════════════════════════════════

void DesktopApp::updateSelectionLabels()
{
	if (m_focusCardsIdx < 0 || m_focusCardsIdx >= (int)m_cards.size())
		return;
	if (m_descLabel)
	{
		lv_label_set_text(m_descLabel, m_cards[m_focusCardsIdx].desc);
	}
	if (m_infoLabel)
	{
		lv_label_set_text_fmt(m_infoLabel, "已选择: %s", m_cards[m_focusCardsIdx].displayName);
	}
}

// ════════════════════════════════════════════════════════════════
// 焦点系统
// ════════════════════════════════════════════════════════════════

lv_obj_t* DesktopApp::statusTarget(int8_t idx)
{
	switch (idx)
	{
	case STATUS_TIME:    return m_timeLabel;
	case STATUS_WIFI:    return m_wifiLabel;
	case STATUS_BT:      return m_bluetoothLabel;
	case STATUS_VOL:     return m_volumeLabel;
	case STATUS_BRIGHT:  return m_brightnessLabel;
	case STATUS_CONSOLE: return m_consoleLabel;
	case STATUS_BATTERY: return m_batteryLabel;
	default:             return nullptr;
	}
}

void DesktopApp::applyFocus()
{
	auto guard = display->lockGuard();
	auto clearFocus = [](lv_obj_t* obj)
		{
			if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED);
		};

	clearFocus(m_timeLabel);
	clearFocus(m_wifiLabel);
	clearFocus(m_bluetoothLabel);
	clearFocus(m_volumeLabel);
	clearFocus(m_brightnessLabel);
	clearFocus(m_consoleLabel);
	clearFocus(m_batteryLabel);
	for (auto* card : m_cardObjs) clearFocus(card);
	clearFocus(m_startBtn);

	// 设置当前聚焦对象的 LV_STATE_FOCUSED
	auto focus = [](lv_obj_t* obj)
		{
			if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED);
		};

	switch (m_focusGroup)
	{
	case FOCUS_CARDS:
		if (m_focusCardsIdx >= 0 && m_focusCardsIdx < (int)m_cardObjs.size())
		{
			focus(m_cardObjs[m_focusCardsIdx]);
			// 刷新布局后滚动到视图
			lv_obj_scroll_to_view(m_cardObjs[m_focusCardsIdx], LV_ANIM_OFF);
		}
		break;

	case FOCUS_BOTTOM:
		focus(m_startBtn);
		break;

	case FOCUS_STATUS:
	{
		if (m_focusStatusIdx >= 0 && m_focusStatusIdx < STATUS_COUNT)
			focus(statusTarget(m_focusStatusIdx));
		break;
	}
	}
}

void DesktopApp::activateFocus()
{
	switch (m_focusGroup)
	{
	case FOCUS_CARDS:
		startGame();
		break;

	case FOCUS_BOTTOM:
		startGame();
		break;

	case FOCUS_STATUS:
	{
		if (auto* t = statusTarget(m_focusStatusIdx))
		{
			auto guard = display->lockGuard();
			lv_obj_send_event(t, LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	}
}

// ── 卡片导航 ──

void DesktopApp::navCardsLeft()
{
	auto guard = display->lockGuard();
	if (m_focusCardsIdx <= 0)
		return;
	m_focusCardsIdx--;
	updateSelectionLabels();
	applyFocus();
	ESP_LOGI(TAG, "已选择: %s (index=%d)", m_cards[m_focusCardsIdx].displayName, m_focusCardsIdx);
}

void DesktopApp::navCardsRight()
{
	auto guard = display->lockGuard();
	if (m_focusCardsIdx >= (int)m_cards.size() - 1)
		return;
	m_focusCardsIdx++;
	updateSelectionLabels();
	applyFocus();
	ESP_LOGI(TAG, "已选择: %s (index=%d)", m_cards[m_focusCardsIdx].displayName, m_focusCardsIdx);
}

// ── 底部导航 ──

void DesktopApp::navToBottom()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_BOTTOM;
	applyFocus();
}

void DesktopApp::navFromBottomUp()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_CARDS;
	applyFocus();
}

// ── 状态栏导航 ──

void DesktopApp::navToStatus()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_STATUS;
	m_focusStatusIdx = STATUS_TIME;
	applyFocus();
}

void DesktopApp::navFromStatusDown()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_CARDS;
	applyFocus();
}

// ── 操作 ──

void DesktopApp::startGame()
{
	if (m_focusCardsIdx < 0 || m_focusCardsIdx >= (int)m_cards.size())
		return;

	ESP_LOGI(TAG, "启动游戏: %s", m_cards[m_focusCardsIdx].displayName);

	if (!m_manager)
	{
		ESP_LOGE(TAG, "startGame: no manager set");
		return;
	}

	const GameCard& c = m_cards[m_focusCardsIdx];
	switch (c.kind)
	{
	case CardKind::Elf:
		// ELF 插件：ElfManager 动态加载（缺 .so / 版本不符 → 优雅提示不崩）
		if (auto* app = ElfManager::instance().createApp(c.elfPath, display, nullptr)) {
			m_manager->pushToNewStack(app);
		} else {
			ESP_LOGW(TAG, "ELF 插件未安装或加载失败（%s）", c.elfPath);
		}
		break;

	case CardKind::Builtin:
		switch (c.index)
		{
		case 0:
			// 水果忍者
			m_manager->pushToNewStack(new FruitNinjaRoom(display));
			break;
		case 1:
			// 俄罗斯方块 — 先打开房间选人
			m_manager->pushToNewStack(new TetrisRoomApp(display));
			break;
		case 2:
			// 斗地主 — 大厅（单机 AI / 多主机联机）
			m_manager->pushToNewStack(new DouDizhuRoom(display));
			break;
		case 3:
			// 中国象棋
			m_manager->pushToNewStack(new ChineseChessRoom(display));
			break;
		}
		break;

	case CardKind::System:
		switch (c.index)
		{
		case 0:
			// Lua 脚本管理器
			m_manager->pushToNewStack(new LuaManagerApp(display));
			break;
		case 1:
			// 游戏市场
			m_manager->pushToNewStack(new MarketApp(display));
			break;
		case 2:
		default:
			// 测试
			m_manager->pushToNewStack(new TestApp(display, esp_random()));
			break;
		}
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// AI 能力
// ════════════════════════════════════════════════════════════════

std::string DesktopApp::getAiContext()
{
	std::string ctx = "当前在桌面（主菜单）。\n"
		"可用 open_app 打开的应用（用 text 指定名称）：\n";

	// 从能力表自动生成（单一数据源，避免手工罗列漂移）
	for (const auto& t : getOpenableApps()) {
		ctx += "- ";
		ctx += t.name;
		ctx += "\t";
		ctx += t.desc;
		ctx += "\n";
	}

	ctx += "\n"
		"可用焦点组（select 的 text 参数）：\n";

	// 卡片组动态生成（ELF 插件 + 内置 + 系统）
	ctx += "- cards(0-" + std::to_string((int)m_cards.size() - 1) + ")\t游戏卡片：";
	for (int i = 0; i < (int)m_cards.size(); i++)
	{
		if (i) ctx += "/";
		ctx += m_cards[i].displayName;
	}
	ctx += "\n"
		"- status(0-6)\t状态栏（时间/WiFi/蓝牙/音量/亮度/多主机/电池）\n"
		"- buttons(0)\t开始游戏按钮\n\n";

	// 当前焦点
	ctx += "焦点@";
	switch (m_focusGroup) {
	case FOCUS_CARDS:  ctx += "cards:" + std::to_string(m_focusCardsIdx); break;
	case FOCUS_BOTTOM: ctx += "buttons:0"; break;
	case FOCUS_STATUS: ctx += "status:" + std::to_string(m_focusStatusIdx); break;
	}
	ctx += "\n";

	return ctx;
}

std::vector<AiAppTarget> DesktopApp::getOpenableApps()
{
	std::vector<AiAppTarget> targets = {
		{ "fruitNinja", "水果忍者", &FruitNinjaRoom::newApp },
		{ "tetris", "俄罗斯方块", &TetrisRoomApp::newApp },
		{ "doudizhu", "斗地主", &DouDizhuRoom::newApp },
		{ "chineseChess", "中国象棋", &ChineseChessRoom::newApp },
		{ "luaManager", "Lua 脚本管理器", &LuaManagerApp::newApp },
		{ "market", "游戏市场", &MarketApp::newApp },
		{ "timeSettings", "时间设置", &TimeSettingsApp::newApp },
		{ "wifiSettings", "WiFi 设置", &WifiSettingsApp::newApp },
		{ "bleSettings", "蓝牙手柄设置", &BleSettingsApp::newApp },
		{ "powerManagement", "电源管理", &PowerManagementApp::newApp },
		{ "consoleManager", "多主机管理", &ConsoleManagerApp::newApp },
		{ "testApp", "测试app", &TestApp::newApp },
	};

	// 动态并入 ELF 插件：AiAppTarget {name, desc, create=nullptr, elfPath}
	//  name=info->name（ASCII，AI open_app 键）；指针指向 m_cards 内，桌面存活期有效
	for (const auto& c : m_cards)
	{
		if (c.kind == CardKind::Elf)
			targets.insert(targets.begin(), { c.pluginName, c.displayName, nullptr, c.elfPath });
	}
	return targets;
}

void DesktopApp::onAiSelect(const AiSelectTarget& target)
{
	auto guard = display->lockGuard();
	if (!guard) return;

	if (target.group == "cards" && target.index >= 0 && target.index < (int)m_cards.size()) {
		m_focusGroup = FOCUS_CARDS;
		m_focusCardsIdx = static_cast<int8_t>(target.index);
		updateSelectionLabels();
		applyFocus();
		ESP_LOGI(TAG, "AI select cards %d: %s", target.index, m_cards[target.index].displayName);
	}
	else if (target.group == "buttons" && target.index == 0) {
		m_focusGroup = FOCUS_BOTTOM;
		applyFocus();
		ESP_LOGI(TAG, "AI select buttons");
	}
	else if (target.group == "status" && target.index >= 0 && target.index < STATUS_COUNT) {
		m_focusGroup = FOCUS_STATUS;
		m_focusStatusIdx = static_cast<int8_t>(target.index);
		applyFocus();
		ESP_LOGI(TAG, "AI select status %d", target.index);
	}
}

void DesktopApp::onAiActivate()
{
	activateFocus();
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void DesktopApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 边沿检测：仅刚按下的按钮有效 ──
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// ── BTN_B: 滑块激活时重置并收起 ──
	// if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	// {
	// }

	// ── 激活 (BTN_A / BTN_L3) ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			activateFocus();
		}
	}

	// ── 摇杆归位判断 ──
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[hostId][playerId] = 0;
		return;
	}
	if (m_nextMoveTime[hostId][playerId] >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[hostId][playerId] = xTaskGetTickCount() + delay;

	switch (m_focusGroup)
	{
	case FOCUS_CARDS:
		if (lxLeft)  navCardsLeft();
		if (lxRight) navCardsRight();
		if (lyUp)    navToStatus();
		if (lyDown)  navToBottom();
		break;

	case FOCUS_BOTTOM:
		if (lyUp) navFromBottomUp();
		break;

	case FOCUS_STATUS:
		// 音量滑块激活时的特殊处理
		if (m_volumeSliderActive && m_focusStatusIdx == 3)
		{
			if (lxLeft) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_volumeSlider);
				v = v < 5 ? 0 : v - 5;
				lv_slider_set_value(m_volumeSlider, v, LV_ANIM_OFF);
				Audio::setMasterVolume(v);
				m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
			}
			if (lxRight) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_volumeSlider);
				v = v > 95 ? 100 : v + 5;
				lv_slider_set_value(m_volumeSlider, v, LV_ANIM_OFF);
				Audio::setMasterVolume(v);
				m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
			}
		}

		// 亮度滑块激活时的特殊处理
		if (m_brightnessSliderActive && m_focusStatusIdx == 4)
		{
			if (lxLeft) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_brightnessSlider);
				v = v < 5 ? 0 : v - 5;
				lv_slider_set_value(m_brightnessSlider, v, LV_ANIM_OFF);
				display->setBrightness(v);
				m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
			}
			if (lxRight) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_brightnessSlider);
				v = v > 95 ? 100 : v + 5;
				lv_slider_set_value(m_brightnessSlider, v, LV_ANIM_OFF);
				display->setBrightness(v);
				m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
			}
		}

		// 非滑块激活时的状态栏导航
		if (!(m_volumeSliderActive && m_focusStatusIdx == 3) &&
			!(m_brightnessSliderActive && m_focusStatusIdx == 4))
		{
			if (lxLeft && m_focusStatusIdx > 0)
			{
				m_focusStatusIdx--;
				applyFocus();
			}
			if (lxRight && m_focusStatusIdx < STATUS_BATTERY)
			{
				m_focusStatusIdx++;
				applyFocus();
			}
		}

		// 下判定，离开状态栏导航
		if (lyDown)
		{
			// 如果音量或亮度滑块激活，先收起再导航
			if (m_volumeSliderActive)
				lv_async_call([](void* param) { static_cast<DesktopApp*>(param)->hideVolumeSlider(); }, this);
			if (m_brightnessSliderActive)
				lv_async_call([](void* param) { static_cast<DesktopApp*>(param)->hideBrightnessSlider(); }, this);
			navFromStatusDown();
		}
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void DesktopApp::onPrevBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_CARDS;
	self->navCardsLeft();
}

void DesktopApp::onNextBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_CARDS;
	self->navCardsRight();
}

void DesktopApp::onStartBtnCb(lv_event_t* e)
{
	auto* app = static_cast<DesktopApp*>(lv_event_get_user_data(e));

	/* 防抖 */
	if (app->m_nextActionTime >= xTaskGetTickCount())
		return;
	app->m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;

	/* LVGL 事件回调中持有 LVGL 锁，栈操作必须延后到 Task 中执行 */
	Task::addTask([](void* param) -> TickType_t
		{
			auto* self = static_cast<DesktopApp*>(param);
			self->m_focusGroup = FOCUS_BOTTOM;
			self->startGame();
			return Task::infinityTime;
		}, "deferredStart", app, 0, Task::Affinity::None);
}

void DesktopApp::onBluetoothLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));

	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = STATUS_BT;

	/* LVGL 事件回调中持锁，栈操作须延后 */
	Task::addTask([](void* param) -> TickType_t
		{
			auto* app = static_cast<DesktopApp*>(param);
			if (app->getManager()) {
				auto* bleApp = new BleSettingsApp(app->getDisplay());
				app->getManager()->pushToNewStack(bleApp);
			}
			return Task::infinityTime;
		}, "openBleSettings", self, 0, Task::Affinity::None);
}

// ════════════════════════════════════════════════════════════════
// 亮度滑块
// ════════════════════════════════════════════════════════════════

void DesktopApp::showBrightnessSlider()
{
	if (m_brightnessSliderActive) return;

	int cur = display->getBrightness();
	m_brightnessOnOpen = cur;

	m_brightnessSlider = lv_slider_create(lv_obj_get_parent(m_brightnessLabel));
	lv_slider_set_range(m_brightnessSlider, 0, 100);
	lv_slider_set_value(m_brightnessSlider, cur, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(m_brightnessSlider, lv_color_hex(0x555555), LV_PART_MAIN);
	lv_obj_set_style_bg_color(m_brightnessSlider, lv_color_white(), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(m_brightnessSlider, lv_color_white(), LV_PART_KNOB);
	lv_obj_set_height(m_brightnessSlider, 28);
	lv_obj_set_flex_grow(m_brightnessSlider, 1);  // 初始 1，立即参与 grow 分配
	lv_obj_set_style_pad_left(m_brightnessSlider, 8, 0);
	lv_obj_set_style_pad_right(m_brightnessSlider, 16, 0);
	lv_obj_move_to_index(m_brightnessSlider, lv_obj_get_index(m_brightnessLabel) + 1);  // 动态定位到亮度标签之后
	lv_obj_add_event_cb(m_brightnessSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		int val = lv_slider_get_value(self->m_brightnessSlider);
		self->display->setBrightness(val);
		self->m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_VALUE_CHANGED, this);
	lv_obj_add_event_cb(m_brightnessSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_RELEASED, this);

	m_brightnessSliderActive = true;
	m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;

	// 创建超时定时器（200ms 周期检查）
	m_brightnessSliderTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		if (!self->m_brightnessSliderActive) {
			lv_timer_del(t);
			return;
		}
		if (xTaskGetTickCount() > self->m_brightnessSliderTimeout) {
			self->hideBrightnessSlider();
		}
		}, 200, this);
	lv_timer_set_repeat_count(m_brightnessSliderTimer, -1);  // 无限重复

	// 动画展开（flex_grow: 1 → 60）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_brightnessSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, 1, SliderGrow);
	lv_anim_set_time(&a, SliderOpenTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_start(&a);

	ESP_LOGI(TAG, "亮度滑块展开: %d%%", cur);
}

void DesktopApp::hideBrightnessSlider()
{
	if (!m_brightnessSliderActive || !m_brightnessSlider) return;
	m_brightnessSliderActive = false;

	auto guard = display->lockGuard();

	// 删除超时定时器
	if (m_brightnessSliderTimer) {
		lv_timer_del(m_brightnessSliderTimer);
		m_brightnessSliderTimer = nullptr;
	}

	// 反向动画收起（flex_grow: 60 → 1）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_brightnessSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, SliderGrow, 1);
	lv_anim_set_time(&a, SliderCloseTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_set_deleted_cb(&a, [](lv_anim_t* anim) {
		if (anim->var)
			lv_obj_delete_async(static_cast<lv_obj_t*>(anim->var));
		});
	lv_anim_start(&a);

	// 值变化时才保存
	if (lv_slider_get_value(m_brightnessSlider) != m_brightnessOnOpen)
		display->saveBrightness();
	m_brightnessSlider = nullptr;
	ESP_LOGI(TAG, "亮度滑块收起");
}

void DesktopApp::onBrightnessSliderCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	int val = lv_slider_get_value(self->m_brightnessSlider);
	self->display->setBrightness(val);
}

// ════════════════════════════════════════════════════════════════
// 音量滑块
// ════════════════════════════════════════════════════════════════

void DesktopApp::showVolumeSlider()
{
	if (m_volumeSliderActive) return;

	int cur = Audio::getMasterVolume();
	m_volumeOnOpen = cur;

	m_volumeSlider = lv_slider_create(lv_obj_get_parent(m_volumeLabel));
	lv_slider_set_range(m_volumeSlider, 0, 100);
	lv_slider_set_value(m_volumeSlider, cur, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(m_volumeSlider, lv_color_hex(0x555555), LV_PART_MAIN);
	lv_obj_set_style_bg_color(m_volumeSlider, lv_color_white(), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(m_volumeSlider, lv_color_white(), LV_PART_KNOB);
	lv_obj_set_height(m_volumeSlider, 28);
	lv_obj_set_flex_grow(m_volumeSlider, 1);  // 初始 1，立即参与 grow 分配
	lv_obj_set_style_pad_left(m_volumeSlider, 8, 0);
	lv_obj_set_style_pad_right(m_volumeSlider, 16, 0);
	lv_obj_move_to_index(m_volumeSlider, lv_obj_get_index(m_volumeLabel) + 1);  // 动态定位到音量标签之后
	lv_obj_add_event_cb(m_volumeSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		int val = lv_slider_get_value(self->m_volumeSlider);
		Audio::setMasterVolume(val);
		self->m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_VALUE_CHANGED, this);
	lv_obj_add_event_cb(m_volumeSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_RELEASED, this);

	m_volumeSliderActive = true;
	m_volumeSliderTimeout = xTaskGetTickCount() + 3000;

	// 创建超时定时器（200ms 周期检查）
	m_volumeSliderTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		if (!self->m_volumeSliderActive) {
			lv_timer_del(t);
			return;
		}
		if (xTaskGetTickCount() > self->m_volumeSliderTimeout) {
			self->hideVolumeSlider();
		}
		}, 200, this);
	lv_timer_set_repeat_count(m_volumeSliderTimer, -1);  // 无限重复

	// 动画展开（flex_grow: 1 → 60）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_volumeSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, 1, SliderGrow);
	lv_anim_set_time(&a, SliderOpenTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_start(&a);

	ESP_LOGI(TAG, "音量滑块展开: %d%%", cur);
}

void DesktopApp::hideVolumeSlider()
{
	if (!m_volumeSliderActive || !m_volumeSlider) return;
	m_volumeSliderActive = false;

	auto guard = display->lockGuard();

	// 删除超时定时器
	if (m_volumeSliderTimer) {
		lv_timer_del(m_volumeSliderTimer);
		m_volumeSliderTimer = nullptr;
	}

	// 反向动画收起（flex_grow: 60 → 1）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_volumeSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, SliderGrow, 1);
	lv_anim_set_time(&a, SliderCloseTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_set_deleted_cb(&a, [](lv_anim_t* anim) {
		if (anim->var)
			lv_obj_delete_async(static_cast<lv_obj_t*>(anim->var));
		});
	lv_anim_start(&a);

	// 值变化时才保存
	if (lv_slider_get_value(m_volumeSlider) != m_volumeOnOpen)
		Audio::saveVolumeToNvs();
	m_volumeSlider = nullptr;
	ESP_LOGI(TAG, "音量滑块收起");
}

void DesktopApp::onVolumeSliderCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	int val = lv_slider_get_value(self->m_volumeSlider);
	Audio::setMasterVolume(val);
}

// ════════════════════════════════════════════════════════════════
// 状态栏图标回调
// ════════════════════════════════════════════════════════════════

void DesktopApp::onVolumeLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = STATUS_VOL;
	if (self->m_volumeSliderActive)
		self->hideVolumeSlider();
	else
		self->showVolumeSlider();
	self->applyFocus();
}

void DesktopApp::onBrightnessLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = STATUS_BRIGHT;
	if (self->m_brightnessSliderActive)
		self->hideBrightnessSlider();
	else
		self->showBrightnessSlider();
	self->applyFocus();
}

void DesktopApp::onBatteryLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = STATUS_BATTERY;

	Task::addTask([](void* param) -> TickType_t
		{
			auto* app = static_cast<DesktopApp*>(param);
			if (app->getManager()) {
				auto* powerApp = new PowerManagementApp(app->getDisplay());
				app->getManager()->pushToNewStack(powerApp);
			}
			return Task::infinityTime;
		}, "openPowerMgr", self, 0, Task::Affinity::NotAssigned);
}

void DesktopApp::updateBatteryIcon()
{
	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGW(TAG, "Failed to lock display @ Desktop::updateBatteryIcon");
		return;
	}

	if (BatteryManager::instance().isCharging())
	{
		BatteryManager::startChargingAnim(m_batteryLabel);
	}
	else
	{
		BatteryManager::stopChargingAnim(m_batteryLabel);

		int pct = BatteryManager::instance().getPercent();
		if (pct < 0) return;
		lv_label_set_text(m_batteryLabel, BatteryManager::getIcon(pct));
		lv_obj_set_style_text_color(m_batteryLabel, BatteryManager::getColor(pct), 0);
	}
}
