#include "app/luaManagerApp/luaManagerApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "storage/fat.hpp"
#include "app/luaGameApp/luaGameApp.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"

#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>
#include "app/appStackManager.hpp"

// ============================================================
//  构造 / 析构
// ============================================================

LuaManagerApp::LuaManagerApp(Display* display)
	: App(display)
{
}

LuaManagerApp::~LuaManagerApp()
{
	if (m_refreshTimer) {
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}
}

// ============================================================
//  scanScripts — 扫描 FAT 上所有 .lua 文件
// ============================================================

void LuaManagerApp::scanScripts()
{
	m_scripts.clear();

	const char* dirs[] = { "/root/lua/game", "/root/lua", "/root/lua/market" };

	for (const char* dir : dirs) {
		DIR* d = opendir(dir);
		if (!d) continue;

		struct dirent* entry;
		while ((entry = readdir(d)) != nullptr) {
			const char* name = entry->d_name;

			// 只处理 .lua 文件
			if (entry->d_type != DT_REG) continue;
			size_t len = strlen(name);
			if (len < 5 || strcasecmp(name + len - 4, ".lua") != 0) continue;

			ScriptEntry se;
			se.name = name;
			se.displayName = std::string(name, len - 4);
			se.path = std::string(dir) + "/" + name;

			// 简短路径前缀
			se.shortPath = std::string(dir + 5);  // 去掉 "/root"，如 "lua/game/" 或 "lua/"
			se.shortPath += "/";

			// 获取文件大小
			struct stat st;
			if (stat(se.path.c_str(), &st) == 0) {
				se.sizeBytes = (size_t)st.st_size;
			}

			m_scripts.push_back(std::move(se));
		}
		closedir(d);
	}

	// 排序：/root/lua/game/ 下的优先（AI 生成的），同目录按修改时间倒序
	// 通过比较路径前缀来保证 game 目录优先
	auto sortKey = [](const ScriptEntry& s) -> std::string {
		if (s.path.find("/root/lua/game/") == 0) return "0" + s.path;
		return "1" + s.path;
		};
	std::sort(m_scripts.begin(), m_scripts.end(),
		[&](const ScriptEntry& a, const ScriptEntry& b) {
			return sortKey(a) < sortKey(b);
		});

	ESP_LOGI(TAG, "扫描到 %zu 个脚本", m_scripts.size());
	if (m_selectedIdx >= (int)m_scripts.size())
		m_selectedIdx = m_scripts.empty() ? -1 : 0;
}

// ============================================================
//  init
// ============================================================

void LuaManagerApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard) {
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}

	// 页面背景
	lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 主容器（flex 列） ──
	m_container = lv_obj_create(screen);
	lv_obj_set_size(m_container, lv_pct(100), lv_pct(100));
	lv_obj_set_layout(m_container, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_border_width(m_container, 0, 0);
	lv_obj_set_style_bg_opa(m_container, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_container, 0, 0);

	// ── 标题行（自适应高度） ──
	m_titleRow = lv_obj_create(m_container);
	lv_obj_set_size(m_titleRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(m_titleRow, 0, 0);
	lv_obj_set_style_bg_color(m_titleRow, LV_COLOR_MAKE(0x15, 0x15, 0x25), 0);
	lv_obj_set_style_bg_opa(m_titleRow, LV_OPA_COVER, 0);
	lv_obj_set_flex_flow(m_titleRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_left(m_titleRow, 12, 0);
	lv_obj_set_style_pad_right(m_titleRow, 12, 0);
	lv_obj_set_style_pad_top(m_titleRow, 6, 0);
	lv_obj_set_scrollbar_mode(m_titleRow, LV_SCROLLBAR_MODE_OFF);

	// 返回按钮
	m_backBtn = GUI::createButton(m_titleRow, LV_SYMBOL_LEFT " 返回", 80, 36);
	lv_obj_set_style_radius(m_backBtn, 6, 0);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_add_event_cb(m_backBtn, [](lv_event_t* e) {
		auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
		self->popApp();
		}, LV_EVENT_CLICKED, this);

	// 标题文字
	auto* title = GUI::createTitle(m_titleRow, "Lua 脚本管理");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// 脚本数量
	auto* countLabel = GUI::createLabel(m_titleRow, "");
	lv_obj_set_style_text_color(countLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_user_data(countLabel, (void*)"count");

	// ── 列表容器（flex_grow 填充中间剩余空间） ──
	m_listContainer = lv_obj_create(m_container);
	lv_obj_set_size(m_listContainer, lv_pct(100), 1);
	lv_obj_set_flex_grow(m_listContainer, 1);
	lv_obj_set_style_border_width(m_listContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_listContainer, 0, 0);
	lv_obj_set_style_pad_left(m_listContainer, 8, 0);
	lv_obj_set_style_pad_right(m_listContainer, 8, 0);
	lv_obj_set_style_radius(m_listContainer, 0, 0);
	lv_obj_set_style_clip_corner(m_listContainer, true, 0);
	lv_obj_set_flex_flow(m_listContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollbar_mode(m_listContainer, LV_SCROLLBAR_MODE_AUTO);

	// 空列表提示
	m_emptyLabel = lv_label_create(m_listContainer);
	lv_label_set_text(m_emptyLabel, "暂无 Lua 脚本\n\n通过 AI 助手生成新游戏\n或手动上传 .lua 文件到 /root/lua/");
	lv_obj_set_style_text_align(m_emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(m_emptyLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_width(m_emptyLabel, lv_pct(100));
	lv_obj_set_style_pad_top(m_emptyLabel, 40, 0);

	// ── 底部操作栏 ──
	m_bottomBar = lv_obj_create(m_container);
	lv_obj_set_size(m_bottomBar, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(m_bottomBar, 0, 0);
	lv_obj_set_style_bg_color(m_bottomBar, LV_COLOR_MAKE(0x15, 0x15, 0x25), 0);
	lv_obj_set_style_bg_opa(m_bottomBar, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(m_bottomBar, 8, 0);
	lv_obj_set_style_pad_top(m_bottomBar, 6, 0);
	lv_obj_set_style_pad_bottom(m_bottomBar, 12, 0);
	lv_obj_set_flex_flow(m_bottomBar, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_style_pad_gap(m_bottomBar, 8, 0);
	lv_obj_set_style_flex_main_place(m_bottomBar, LV_FLEX_ALIGN_CENTER, 0);
	lv_obj_set_scrollbar_mode(m_bottomBar, LV_SCROLLBAR_MODE_OFF);

	const char* btnLabels[] = { LV_SYMBOL_OK "运行",LV_SYMBOL_TRASH "删除", LV_SYMBOL_EDIT "重命名", LV_SYMBOL_PLUS "AI 生成" };
	for (int i = 0; i < BOTTOM_BTN_COUNT; i++) {
		m_bottomBtns[i] = GUI::createButton(m_bottomBar, btnLabels[i], 140, 36);
		lv_obj_set_style_radius(m_bottomBtns[i], 6, 0);
		lv_obj_set_style_border_width(m_bottomBtns[i], 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(m_bottomBtns[i], lv_color_white(), LV_STATE_FOCUSED);

		// 最后一个按钮（AI生成）强调色
		if (i == 3) {
			lv_obj_set_style_bg_color(m_bottomBtns[i], LV_COLOR_MAKE(0x00, 0x66, 0xcc), 0);
		}
		else {
			lv_obj_set_style_bg_color(m_bottomBtns[i], LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
		}
	}

	// 按钮事件
	lv_obj_add_event_cb(m_bottomBtns[0], [](lv_event_t* e) {
		auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
		self->runSelected();
		}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(m_bottomBtns[1], [](lv_event_t* e) {
		auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
		self->deleteSelected();
		}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(m_bottomBtns[2], [](lv_event_t* e) {
		auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
		self->startRename();
		}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(m_bottomBtns[3], [](lv_event_t* e) {
		auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
		self->startAiGeneration();
		}, LV_EVENT_CLICKED, this);

	// ── 扫描脚本并构建列表 ──
	scanScripts();
	rebuildList();
	applyFocus();

	ESP_LOGI(TAG, "LuaManagerApp 初始化完成 (%zu 脚本)", m_scripts.size());
}

// ============================================================
//  deinit
// ============================================================

void LuaManagerApp::deinit()
{
	if (m_refreshTimer)
	{
		auto guard = display->lockGuard();
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}
	// 清理确认弹框（它在 lv_layer_top 上，不清理会残留）
	if (m_confirmBox)
	{
		auto guard = display->lockGuard();
		lv_obj_del(m_confirmBox);
		m_confirmBox = nullptr;
		m_confirmLabel = nullptr;
	}
	App::deinit();
}

// ============================================================
//  onForeground / onBackground
// ============================================================

void LuaManagerApp::onForeground()
{
	App::onForeground();
	// 刷新脚本列表
	scanScripts();
	auto guard = display->lockGuard();
	if (guard) {
		rebuildList();
		applyFocus();
	}
	// 重置去重
	m_nextActionTime = 0;
	for (auto& i : m_prevButtons)
		for (auto& j : i)
			j = 0xFFFF;
	for (auto& row : m_nextMoveTime)
		for (auto& t : row)
			t = 0;
}

void LuaManagerApp::onBackground()
{
	App::onBackground();
}

// ============================================================
//  列表重建
// ============================================================

void LuaManagerApp::rebuildList()
{
	// 清空现有行
	for (auto* row : m_entryRows) {
		lv_obj_del(row);
	}
	m_entryRows.clear();
	m_entryInfoLabels.clear();

	// 显示/隐藏空列表提示
	if (m_emptyLabel) {
		if (m_scripts.empty()) {
			lv_obj_clear_flag(m_emptyLabel, LV_OBJ_FLAG_HIDDEN);
		}
		else {
			lv_obj_add_flag(m_emptyLabel, LV_OBJ_FLAG_HIDDEN);
		}
	}

	// 更新计数标签
	lv_obj_t* countLabel = lv_obj_get_child(m_titleRow, lv_obj_get_child_cnt(m_titleRow) - 1);
	if (countLabel && lv_obj_get_user_data(countLabel) == (void*)"count") {
		char buf[32];
		snprintf(buf, sizeof(buf), "%zu 个脚本", m_scripts.size());
		lv_label_set_text(countLabel, buf);
	}

	auto* smlFont = FontLoader::getDefault(FontLoader::FontSize::Small);

	// 创建行
	for (size_t i = 0; i < m_scripts.size(); i++) {
		const auto& se = m_scripts[i];

		auto* row = lv_obj_create(m_listContainer);
		lv_obj_set_size(row, lv_pct(100), ROW_H);
		lv_obj_set_style_border_width(row, 0, 0);
		lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_color(row, LV_COLOR_MAKE(0x25, 0x25, 0x35), 0);
		lv_obj_set_style_bg_color(row, LV_COLOR_MAKE(0x18, 0x18, 0x28), 0);
		lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_left(row, 16, 0);
		lv_obj_set_style_pad_right(row, 16, 0);
		lv_obj_set_style_pad_top(row, 4, 0);
		lv_obj_set_style_pad_bottom(row, 4, 0);
		lv_obj_set_style_radius(row, 0, 0);
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);   // 行不可独立滚动（lv_obj_create 默认可滚动）
		lv_obj_set_user_data(row, (void*)(uintptr_t)i);

		// 聚焦样式
		lv_obj_set_style_bg_color(row, LV_COLOR_MAKE(0x25, 0x35, 0x50), LV_STATE_FOCUSED);
		lv_obj_set_style_outline_width(row, 2, LV_STATE_FOCUSED);
		lv_obj_set_style_outline_color(row, LV_COLOR_MAKE(0x00, 0x88, 0xff), LV_STATE_FOCUSED);
		lv_obj_set_style_outline_pad(row, 1, LV_STATE_FOCUSED);

		// 点击选中
		lv_obj_add_event_cb(row, [](lv_event_t* e) {
			auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
			lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
			int idx = (int)(uintptr_t)lv_obj_get_user_data(target);
			if (idx >= 0 && idx < (int)self->m_scripts.size()) {
				self->m_focusGroup = FOCUS_LIST;
				self->m_selectedIdx = (int8_t)idx;
				self->applyFocus();
			}
			}, LV_EVENT_CLICKED, this);

		// 行内 flex：名称居左，大小/时间居右
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_flex_main_place(row, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
		lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_CENTER, 0);

		// 左侧：▶ + 名称
		auto* leftArea = lv_obj_create(row);
		lv_obj_set_size(leftArea, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_flex_flow(leftArea, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_border_width(leftArea, 0, 0);
		lv_obj_set_style_bg_opa(leftArea, LV_OPA_TRANSP, 0);
		lv_obj_set_style_pad_all(leftArea, 0, 0);
		lv_obj_clear_flag(leftArea, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(leftArea, LV_OBJ_FLAG_SCROLLABLE);   // 内容区不可独立滚动

		auto* nameLabel = lv_label_create(leftArea);
		lv_label_set_text(nameLabel, se.displayName.c_str());
		lv_obj_set_style_text_color(nameLabel, GUI::Color::TEXT, 0);

		// 右侧：路径前缀 + 大小
		auto* rightArea = lv_obj_create(row);
		lv_obj_set_size(rightArea, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_flex_flow(rightArea, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_border_width(rightArea, 0, 0);
		lv_obj_set_style_bg_opa(rightArea, LV_OPA_TRANSP, 0);
		lv_obj_set_style_pad_all(rightArea, 0, 0);
		lv_obj_set_style_pad_gap(rightArea, 12, 0);
		lv_obj_clear_flag(rightArea, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(rightArea, LV_OBJ_FLAG_SCROLLABLE);   // 内容区不可独立滚动

		// 简短路径
		auto* pathLabel = lv_label_create(rightArea);
		lv_label_set_text(pathLabel, se.shortPath.c_str());
		lv_obj_set_style_text_color(pathLabel, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(pathLabel, smlFont, 0);

		// 格式化大小
		char sizeStr[16];
		if (se.sizeBytes < 1024)
			snprintf(sizeStr, sizeof(sizeStr), "%zu B", se.sizeBytes);
		else if (se.sizeBytes < 1024 * 1024)
			snprintf(sizeStr, sizeof(sizeStr), "%.1f KB", se.sizeBytes / 1024.0f);
		else
			snprintf(sizeStr, sizeof(sizeStr), "%.1f MB", se.sizeBytes / (1024.0f * 1024.0f));

		auto* sizeLabel = lv_label_create(rightArea);
		lv_label_set_text(sizeLabel, sizeStr);
		lv_obj_set_style_text_color(sizeLabel, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(sizeLabel, smlFont, 0);

		m_entryRows.push_back(row);
		m_entryInfoLabels.push_back(pathLabel);
	}
}

// ============================================================
//  操作实现
// ============================================================

void LuaManagerApp::runSelected()
{
	if (m_selectedIdx < 0 || m_selectedIdx >= (int)m_scripts.size()) return;

	const auto& se = m_scripts[m_selectedIdx];
	ESP_LOGI(TAG, "运行脚本: %s", se.path.c_str());

	if (!m_manager) return;
	m_manager->push(new LuaGameApp(display, se.path.c_str()));
}

void LuaManagerApp::deleteSelected()
{
	if (m_selectedIdx < 0 || m_selectedIdx >= (int)m_scripts.size()) return;
	if (m_mode != MODE_BROWSE) return;

	m_mode = MODE_CONFIRM_DEL;
	m_pendingDeletePath = m_scripts[m_selectedIdx].path;

	// 创建确认弹框
	if (!m_confirmBox) {
		m_confirmBox = lv_obj_create(lv_layer_top());
		lv_obj_set_size(m_confirmBox, 420, 200);
		lv_obj_center(m_confirmBox);
		lv_obj_set_style_bg_color(m_confirmBox, LV_COLOR_MAKE(0x2d, 0x2d, 0x3d), 0);
		lv_obj_set_style_bg_opa(m_confirmBox, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(m_confirmBox, 16, 0);
		lv_obj_set_style_border_width(m_confirmBox, 1, 0);
		lv_obj_set_style_border_color(m_confirmBox, LV_COLOR_MAKE(0x55, 0x55, 0x66), 0);
		lv_obj_set_style_shadow_width(m_confirmBox, 40, 0);
		lv_obj_set_style_shadow_color(m_confirmBox, LV_COLOR_MAKE(0x00, 0x00, 0x00), 0);
		lv_obj_set_style_shadow_opa(m_confirmBox, LV_OPA_60, 0);
		lv_obj_set_layout(m_confirmBox, LV_LAYOUT_NONE);
		lv_obj_set_style_pad_all(m_confirmBox, 0, 0);
		lv_obj_set_style_text_font(m_confirmBox,
			FontLoader::getDefault(FontLoader::FontSize::Default), 0);

		// 提示文字（居中偏上）
		m_confirmLabel = lv_label_create(m_confirmBox);
		lv_obj_set_style_text_align(m_confirmLabel, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_text_color(m_confirmLabel, GUI::Color::TEXT, 0);
		lv_obj_align(m_confirmLabel, LV_ALIGN_TOP_MID, 0, 40);

		// 底部按钮行容器
		auto* btnRow = lv_obj_create(m_confirmBox);
		lv_obj_set_size(btnRow, lv_pct(100), 56);
		lv_obj_align(btnRow, LV_ALIGN_BOTTOM_MID, 0, 0);
		lv_obj_set_style_border_width(btnRow, 0, 0);
		lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
		lv_obj_set_style_pad_all(btnRow, 0, 0);
		lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_flex_main_place(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
		lv_obj_set_style_flex_cross_place(btnRow, LV_FLEX_ALIGN_CENTER, 0);

		m_confirmCancelBtn = GUI::createButton(btnRow, "  取消  ", 130, 44);
		lv_obj_set_style_bg_color(m_confirmCancelBtn, LV_COLOR_MAKE(0x55, 0x55, 0x66), 0);
		lv_obj_set_style_radius(m_confirmCancelBtn, 10, 0);
		lv_obj_set_style_border_width(m_confirmCancelBtn, 2, 0);
		lv_obj_set_style_border_color(m_confirmCancelBtn, LV_COLOR_MAKE(0x88, 0x88, 0x99), 0);
		lv_obj_set_style_border_color(m_confirmCancelBtn, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_border_opa(m_confirmCancelBtn, LV_OPA_COVER, LV_STATE_FOCUSED);
		lv_obj_add_event_cb(m_confirmCancelBtn, [](lv_event_t* e) {
			auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
			self->m_mode = MODE_BROWSE;
			lv_obj_add_flag(self->m_confirmBox, LV_OBJ_FLAG_HIDDEN);
			}, LV_EVENT_CLICKED, this);

		m_confirmOkBtn = GUI::createButton(btnRow, "确认删除", 150, 44);
		lv_obj_set_style_bg_color(m_confirmOkBtn, LV_COLOR_MAKE(0xdd, 0x33, 0x33), 0);
		lv_obj_set_style_radius(m_confirmOkBtn, 10, 0);
		lv_obj_set_style_border_width(m_confirmOkBtn, 2, 0);
		lv_obj_set_style_border_color(m_confirmOkBtn, LV_COLOR_MAKE(0xff, 0x66, 0x66), 0);
		lv_obj_set_style_border_color(m_confirmOkBtn, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_border_opa(m_confirmOkBtn, LV_OPA_COVER, LV_STATE_FOCUSED);
		lv_obj_add_event_cb(m_confirmOkBtn, [](lv_event_t* e) {
			auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
			self->confirmDelete();
			lv_obj_add_flag(self->m_confirmBox, LV_OBJ_FLAG_HIDDEN);
			}, LV_EVENT_CLICKED, this);

		// 初始隐藏
		lv_obj_add_flag(m_confirmBox, LV_OBJ_FLAG_HIDDEN);
	}

	lv_obj_clear_flag(m_confirmBox, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(m_confirmBox);
	m_focusGroup = FOCUS_CONFIRM;
	m_focusConfirmIdx = 0;
	applyFocus();

	char buf[128];
	snprintf(buf, sizeof(buf), "确认删除 \"%s\" ？\n此操作不可恢复。",
		m_scripts[m_selectedIdx].displayName.c_str());
	lv_label_set_text(m_confirmLabel, buf);
}

void LuaManagerApp::confirmDelete()
{
	if (m_pendingDeletePath.empty()) return;

	ESP_LOGI(TAG, "删除脚本: %s", m_pendingDeletePath.c_str());
	removeFile(m_pendingDeletePath.c_str());
	m_pendingDeletePath.clear();

	m_mode = MODE_BROWSE;

	// 重新扫描和重建
	scanScripts();
	auto guard = display->lockGuard();
	if (guard) {
		rebuildList();
		applyFocus();
	}
}

void LuaManagerApp::startRename()
{
	if (m_selectedIdx < 0 || m_selectedIdx >= (int)m_scripts.size()) return;
	if (m_mode != MODE_BROWSE) return;

	m_mode = MODE_RENAME_INPUT;

	const auto& se = m_scripts[m_selectedIdx];

	auto guard = display->lockGuard();

	// 隐藏列表和底部栏
	lv_obj_add_flag(m_listContainer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_bottomBar, LV_OBJ_FLAG_HIDDEN);

	// 创建重命名区域（仅首次）
	if (!m_renameField) {
		m_renameContainer = lv_obj_create(m_container);
		lv_obj_set_size(m_renameContainer, lv_pct(100), LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(m_renameContainer, 0, 0);
		lv_obj_set_style_bg_opa(m_renameContainer, LV_OPA_TRANSP, 0);
		lv_obj_set_flex_flow(m_renameContainer, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_style_pad_all(m_renameContainer, 16, 0);
		lv_obj_set_style_pad_top(m_renameContainer, 30, 0);
		lv_obj_set_scrollbar_mode(m_renameContainer, LV_SCROLLBAR_MODE_OFF);

		auto* hint = lv_label_create(m_renameContainer);
		lv_label_set_text(hint, "输入新名称（无需 .lua 后缀）:");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);

		m_renameField = lv_textarea_create(m_renameContainer);
		lv_obj_set_width(m_renameField, lv_pct(100));
		lv_textarea_set_one_line(m_renameField, true);
		lv_obj_set_style_bg_color(m_renameField, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
		lv_obj_set_style_text_color(m_renameField, GUI::Color::TEXT, 0);
		lv_obj_set_style_radius(m_renameField, 6, 0);
		lv_textarea_set_placeholder_text(m_renameField, "输入新名称...");

		auto* btnRow = lv_obj_create(m_renameContainer);
		lv_obj_set_width(btnRow, lv_pct(100));
		lv_obj_set_height(btnRow, LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(btnRow, 0, 0);
		lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
		lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_flex_main_place(btnRow, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
		lv_obj_set_style_pad_top(btnRow, 12, 0);
		lv_obj_set_scrollbar_mode(btnRow, LV_SCROLLBAR_MODE_OFF);

		auto* cancelBtn = GUI::createButton(btnRow, "取消", 120, 40);
		lv_obj_set_style_bg_color(cancelBtn, LV_COLOR_MAKE(0x40, 0x40, 0x50), 0);
		lv_obj_add_event_cb(cancelBtn, [](lv_event_t* e) {
			auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
			self->cancelRename();
			}, LV_EVENT_CLICKED, this);

		auto* okBtn = GUI::createButton(btnRow, "确认", 120, 40);
		lv_obj_set_style_bg_color(okBtn, LV_COLOR_MAKE(0x00, 0x66, 0xcc), 0);
		lv_obj_add_event_cb(okBtn, [](lv_event_t* e) {
			auto* self = static_cast<LuaManagerApp*>(lv_event_get_user_data(e));
			self->submitRename();
			}, LV_EVENT_CLICKED, this);

		// 创建 keyboard（放在 m_renameContainer 后面）
		m_renameKeyboard = lv_keyboard_create(m_container);
		lv_obj_set_size(m_renameKeyboard, lv_pct(100), 180);
	}
	m_renameKeyboardVisible = true;

	// 设置初始值（去掉 .lua 后缀的当前名称）
	lv_textarea_set_text(m_renameField, se.displayName.c_str());
	lv_textarea_set_cursor_pos(m_renameField, LV_TEXTAREA_CURSOR_LAST);
	lv_keyboard_set_textarea(m_renameKeyboard, m_renameField);

	// 显示重命名区域和键盘
	lv_obj_clear_flag(m_renameContainer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_renameKeyboard, LV_OBJ_FLAG_HIDDEN);
}

void LuaManagerApp::submitRename()
{
	if (m_selectedIdx < 0 || m_selectedIdx >= (int)m_scripts.size()) return;

	const char* newName = lv_textarea_get_text(m_renameField);
	if (!newName || strlen(newName) == 0) {
		cancelRename();
		return;
	}

	const auto& se = m_scripts[m_selectedIdx];

	// 构造新路径（保留原目录）
	std::string dir;
	size_t slashPos = se.path.rfind('/');
	if (slashPos != std::string::npos)
		dir = se.path.substr(0, slashPos + 1);
	else
		dir = "/root/lua/";

	std::string newPath = dir + newName;
	// 确保有 .lua 后缀
	if (newPath.size() < 4 ||
		strcasecmp(newPath.c_str() + newPath.size() - 4, ".lua") != 0) {
		newPath += ".lua";
	}

	ESP_LOGI(TAG, "重命名: %s → %s", se.path.c_str(), newPath.c_str());

	if (moveFile(se.path.c_str(), newPath.c_str())) {
		ESP_LOGI(TAG, "重命名成功");
	}
	else {
		ESP_LOGE(TAG, "重命名失败");
	}

	cancelRename();
}

void LuaManagerApp::cancelRename()
{
	m_mode = MODE_BROWSE;

	// 恢复列表和底部栏可见
	lv_obj_clear_flag(m_listContainer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_bottomBar, LV_OBJ_FLAG_HIDDEN);

	// 隐藏重命名区域和键盘
	if (m_renameContainer) lv_obj_add_flag(m_renameContainer, LV_OBJ_FLAG_HIDDEN);
	if (m_renameKeyboard) {
		lv_obj_add_flag(m_renameKeyboard, LV_OBJ_FLAG_HIDDEN);
		m_renameKeyboardVisible = false;
	}

	// 刷新列表
	scanScripts();
	auto guard = display->lockGuard();
	if (guard) {
		rebuildList();
		applyFocus();
	}
}

void LuaManagerApp::startAiGeneration()
{
	ESP_LOGI(TAG, "唤起 AI 生成");
	AiAgentOverlay::instance().requestShow();
}

// ============================================================
//  焦点导航
// ============================================================

void LuaManagerApp::applyFocus()
{
	// 先清除所有对象的焦点状态
	for (auto* row : m_entryRows) {
		lv_obj_clear_state(row, LV_STATE_FOCUSED);
	}
	for (int i = 0; i < BOTTOM_BTN_COUNT; i++) {
		lv_obj_clear_state(m_bottomBtns[i], LV_STATE_FOCUSED);
	}
	if (m_confirmCancelBtn) lv_obj_clear_state(m_confirmCancelBtn, LV_STATE_FOCUSED);
	if (m_confirmOkBtn) lv_obj_clear_state(m_confirmOkBtn, LV_STATE_FOCUSED);

	// 再设置当前焦点组的焦点
	switch (m_focusGroup) {
	case FOCUS_LIST:
		if (m_selectedIdx >= 0 && m_selectedIdx < (int)m_entryRows.size()) {
			lv_obj_add_state(m_entryRows[m_selectedIdx], LV_STATE_FOCUSED);
			lv_obj_scroll_to_view(m_entryRows[m_selectedIdx], LV_ANIM_OFF);
		}
		break;

	case FOCUS_BOTTOM:
	{
		if (m_focusBottomIdx >= 0 && m_focusBottomIdx < BOTTOM_BTN_COUNT) {
			lv_obj_add_state(m_bottomBtns[m_focusBottomIdx], LV_STATE_FOCUSED);
		}
		break;
	}
	case FOCUS_CONFIRM:
		if (m_focusConfirmIdx == 0 && m_confirmCancelBtn) {
			lv_obj_add_state(m_confirmCancelBtn, LV_STATE_FOCUSED);
		}
		else if (m_focusConfirmIdx == 1 && m_confirmOkBtn) {
			lv_obj_add_state(m_confirmOkBtn, LV_STATE_FOCUSED);
		}
		break;
	}
}

void LuaManagerApp::selectNext()
{
	if (m_scripts.empty()) return;
	if (m_selectedIdx < (int)m_scripts.size() - 1) {
		m_selectedIdx++;
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

void LuaManagerApp::selectPrev()
{
	if (m_scripts.empty()) return;
	if (m_selectedIdx > 0) {
		m_selectedIdx--;
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

void LuaManagerApp::selectFirst()
{
	if (m_scripts.empty()) return;
	m_selectedIdx = 0;
	auto guard = display->lockGuard();
	if (guard) applyFocus();
}

void LuaManagerApp::selectLast()
{
	if (m_scripts.empty()) return;
	m_selectedIdx = (int)m_scripts.size() - 1;
	auto guard = display->lockGuard();
	if (guard) applyFocus();
}

// ============================================================
//  手柄输入
// ============================================================

void LuaManagerApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	TickType_t now = xTaskGetTickCount();

	// ── 按钮动作（边沿检测，用 m_nextActionTime 作全局去重） ──
	uint16_t newPress = state.buttons & ~m_prevButtons[hostId][playerId];
	m_prevButtons[hostId][playerId] = state.buttons;

	// ── 摇杆导航（独立延迟，每设备每玩家独立跟踪） ──
	bool hasMove = lxLeft || lxRight || lyUp || lyDown;
	if (!hasMove)
		m_nextMoveTime[hostId][playerId] = 0;
	else if (m_nextMoveTime[hostId][playerId] >= now)
		hasMove = false;
	else
	{
		TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
		m_nextMoveTime[hostId][playerId] = now + delay;
	}

	// 按钮动作全局去重（仅 newPress 穿透）
	if (!newPress && now < m_nextActionTime) return;

	switch (m_mode)
	{
	case MODE_BROWSE:
	{
		switch (m_focusGroup)
		{
		case FOCUS_LIST:
		{
			if (hasMove && lyUp)
				selectPrev();
			else if (hasMove && lyDown)
				selectNext();
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A) || newPress & static_cast<uint16_t>(GamepadButton::BTN_L3) || (hasMove && lxRight) || (hasMove && lxLeft))
			{
				m_nextActionTime = now + ACTION_COOLDOWN;
				m_focusGroup = FOCUS_BOTTOM;
				auto guard = display->lockGuard();
				if (guard) applyFocus();
			}
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
			{
				m_nextActionTime = now + ACTION_COOLDOWN;
				popApp();
			}
			break;
		}
		case FOCUS_BOTTOM:
		{
			if (hasMove && lxLeft)
			{
				if (m_focusBottomIdx > 0)
				{
					m_focusBottomIdx--;
					auto guard = display->lockGuard();
					if (guard) applyFocus();
				}
			}
			else if (hasMove && lxRight)
			{
				if (m_focusBottomIdx < BOTTOM_BTN_COUNT - 1)
				{
					m_focusBottomIdx++;
					auto guard = display->lockGuard();
					if (guard) applyFocus();
				}
			}
			else if (hasMove && lyUp)
			{
				m_focusGroup = FOCUS_LIST;
				auto guard = display->lockGuard();
				if (guard) applyFocus();
			}
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A))
			{
				m_nextActionTime = now + ACTION_COOLDOWN;
				switch (m_focusBottomIdx)
				{
				case 0: runSelected(); break;
				case 1: deleteSelected(); break;
				case 2: startRename(); break;
				case 3: startAiGeneration(); break;
				}
			}
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
			{
				m_nextActionTime = now + ACTION_COOLDOWN;
				popApp();
			}
			break;
		}
		case FOCUS_CONFIRM: break;
		}
		break;
	}
	case MODE_CONFIRM_DEL:
	{
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
		{
			m_nextActionTime = now + ACTION_COOLDOWN;
			m_mode = MODE_BROWSE;
			if (m_confirmBox)
			{
				auto guard = display->lockGuard();
				if (guard) lv_obj_add_flag(m_confirmBox, LV_OBJ_FLAG_HIDDEN);
			}
			m_focusGroup = FOCUS_BOTTOM;
			applyFocus();
		}
		else if (hasMove && lxLeft && m_focusConfirmIdx > 0)
		{
			m_focusConfirmIdx--;
			auto guard = display->lockGuard();
			if (guard) applyFocus();
		}
		else if (hasMove && lxRight && m_focusConfirmIdx < 1)
		{
			m_focusConfirmIdx++;
			auto guard = display->lockGuard();
			if (guard) applyFocus();
		}
		else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A))
		{
			m_nextActionTime = now + ACTION_COOLDOWN;
			m_mode = MODE_BROWSE;
			m_focusGroup = FOCUS_BOTTOM;
			if (m_confirmBox)
			{
				auto guard = display->lockGuard();
				if (guard)
				{
					lv_obj_add_flag(m_confirmBox, LV_OBJ_FLAG_HIDDEN);
					if (m_focusConfirmIdx == 1)
						confirmDelete();
				}
			}
			applyFocus();
		}
		break;
	}
	case MODE_RENAME_INPUT:
	{
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
		{
			m_nextActionTime = now + ACTION_COOLDOWN;
			cancelRename();
		}
		break;
	}
	}
}

// ============================================================
//  AI 能力
// ============================================================

std::string LuaManagerApp::getAiContext()
{
	std::string ctx = "当前在 Lua 脚本管理器。\n"
		"可用焦点组（select 的 text 参数）：\n"
		"- list(0-N)\t脚本列表\n"
		"- actions(0-3)\t操作按钮（运行/删除/重命名/启动ai overlay（目前已启动））\n\n"
		"当前可用脚本：\n";

	if (m_scripts.empty()) {
		ctx += "  暂无脚本。可通过 generate_lua_game 操作生成新游戏。\n";
	}
	else {
		for (size_t i = 0; i < m_scripts.size(); i++) {
			const auto& se = m_scripts[i];
			ctx += "  " + std::to_string(i) + ": " + se.path
				+ " (" + std::to_string(se.sizeBytes) + " bytes)\n";
		}
	}

	ctx += "\n选中索引: " + std::to_string(m_selectedIdx) + "\n";
	if (m_selectedIdx >= 0 && m_selectedIdx < (int)m_scripts.size()) {
		ctx += "选中脚本: " + m_scripts[m_selectedIdx].path + "\n";
	}

	return ctx;
}

void LuaManagerApp::onAiSelect(const AiSelectTarget& target)
{
	if (target.group == "list") {
		m_focusGroup = FOCUS_LIST;
		if (target.index >= 0 && target.index < (int)m_scripts.size()) {
			m_selectedIdx = (int8_t)target.index;
		}
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
	else if (target.group == "actions") {
		m_focusGroup = FOCUS_BOTTOM;
		if (target.index >= 0 && target.index < BOTTOM_BTN_COUNT) {
			m_focusBottomIdx = (int8_t)target.index;
		}
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

void LuaManagerApp::onAiActivate()
{
	switch (m_focusGroup) {
	case FOCUS_LIST:
	{
		runSelected();
		break;
	}
	case FOCUS_BOTTOM:
	{
		switch (m_focusBottomIdx) {
		case 0: runSelected(); break;
		case 1: deleteSelected(); break;
		case 2: startRename(); break;
		case 3: startAiGeneration(); break;
		}
		break;
	}
	case FOCUS_CONFIRM: break;
	}
}
