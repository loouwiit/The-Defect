#include "app/marketApp/marketApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "storage/fat.hpp"
#include "app/luaGameApp/luaGameApp.hpp"
#include "app/appStackManager.hpp"
#include "market/marketConfig.hpp"

#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <dirent.h>

// ============================================================
//  构造 / 析构
// ============================================================

MarketApp::MarketApp(Display* display)
	: App(display)
{
}

MarketApp::~MarketApp() = default;

// ============================================================
//  setStatus — 更新状态标签
// ============================================================

void MarketApp::setStatus(const char* text, lv_color_t color)
{
	if (!m_statusLabel) return;
	lv_label_set_text(m_statusLabel, text);
	lv_obj_set_style_text_color(m_statusLabel, color, 0);
}

// ============================================================
//  cleanupLegacyFlatFiles — 清理旧版扁平市场文件
// ============================================================

// 旧版把市场游戏存为 /root/lua/market/<id>.lua 扁平文件，直接删除不做迁移。
// 新结构为 /root/lua/market/<作者>/<游戏>/，此处仅清理顶层 .lua，不动作者子目录。
void MarketApp::cleanupLegacyFlatFiles()
{
	DIR* d = opendir("/root/lua/market");
	if (!d) return;
	struct dirent* entry;
	while ((entry = readdir(d)) != nullptr) {
		const char* name = entry->d_name;
		if (entry->d_type != DT_REG) continue;
		size_t len = strlen(name);
		if (len < 5 || strcasecmp(name + len - 4, ".lua") != 0) continue;
		std::string path = std::string("/root/lua/market/") + name;
		ESP_LOGI(TAG, "清理旧版扁平市场文件: %s", path.c_str());
		removeFile(path.c_str());
	}
	closedir(d);
}

// ============================================================
//  init
// ============================================================

void MarketApp::init()
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

	// ── 标题行 ──
	m_titleRow = lv_obj_create(m_container);
	lv_obj_set_size(m_titleRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(m_titleRow, 0, 0);
	lv_obj_set_style_bg_color(m_titleRow, LV_COLOR_MAKE(0x15, 0x15, 0x25), 0);
	lv_obj_set_style_bg_opa(m_titleRow, LV_OPA_COVER, 0);
	lv_obj_set_flex_flow(m_titleRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_left(m_titleRow, 12, 0);
	lv_obj_set_style_pad_right(m_titleRow, 12, 0);
	lv_obj_set_style_pad_top(m_titleRow, 6, 0);
	lv_obj_set_style_pad_bottom(m_titleRow, 6, 0);
	lv_obj_set_scrollbar_mode(m_titleRow, LV_SCROLLBAR_MODE_OFF);

	// 返回按钮
	m_backBtn = GUI::createButton(m_titleRow, LV_SYMBOL_LEFT " 返回", 80, 36);
	lv_obj_set_style_radius(m_backBtn, 6, 0);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_add_event_cb(m_backBtn, [](lv_event_t* e) {
		auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
		self->popApp();
		}, LV_EVENT_CLICKED, this);

	auto* title = GUI::createTitle(m_titleRow, "游戏市场");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// ── 列表容器（flex_grow 填充中间） ──
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

	// ── 状态标签 ──
	m_statusLabel = GUI::createLabel(m_container, "");
	lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_pad_left(m_statusLabel, 12, 0);
	lv_obj_set_style_pad_right(m_statusLabel, 12, 0);
	lv_obj_set_style_pad_bottom(m_statusLabel, 4, 0);
	lv_label_set_long_mode(m_statusLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);

	// ── 底部操作栏 ──
	m_bottomBar = lv_obj_create(m_container);
	lv_obj_set_size(m_bottomBar, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(m_bottomBar, 0, 0);
	lv_obj_set_style_bg_color(m_bottomBar, LV_COLOR_MAKE(0x15, 0x15, 0x25), 0);
	lv_obj_set_style_bg_opa(m_bottomBar, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(m_bottomBar, 8, 0);
	lv_obj_set_style_pad_top(m_bottomBar, 6, 0);
	lv_obj_set_style_pad_bottom(m_bottomBar, 12, 0);
	lv_obj_set_flex_flow(m_bottomBar, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_gap(m_bottomBar, 8, 0);
	lv_obj_set_style_flex_main_place(m_bottomBar, LV_FLEX_ALIGN_CENTER, 0);
	lv_obj_set_scrollbar_mode(m_bottomBar, LV_SCROLLBAR_MODE_OFF);

	const char* btnLabels[] = { LV_SYMBOL_REFRESH "刷新", "详情", LV_SYMBOL_OK "下载运行" };
	for (int i = 0; i < BOTTOM_BTN_COUNT; i++) {
		m_bottomBtns[i] = GUI::createButton(m_bottomBar, btnLabels[i], 150, 38);
		lv_obj_set_style_radius(m_bottomBtns[i], 6, 0);
		lv_obj_set_style_border_width(m_bottomBtns[i], 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(m_bottomBtns[i], lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_bg_color(m_bottomBtns[i], LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
		if (i == 2) {
			// "下载运行" 强调色
			lv_obj_set_style_bg_color(m_bottomBtns[i], LV_COLOR_MAKE(0x00, 0x66, 0xcc), 0);
		}
	}

	lv_obj_add_event_cb(m_bottomBtns[0], [](lv_event_t* e) {
		auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
		self->fetchList();
		}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(m_bottomBtns[1], [](lv_event_t* e) {
		auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
		self->showDetail();
		}, LV_EVENT_CLICKED, this);

	lv_obj_add_event_cb(m_bottomBtns[2], [](lv_event_t* e) {
		auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
		self->downloadAndRun();
		}, LV_EVENT_CLICKED, this);

	// 读取服务器配置
	m_serverUrl = marketConfigGetUrl();

	// 一次性清理旧版扁平市场文件（直接删除，不做迁移）
	cleanupLegacyFlatFiles();

	// 初始空列表
	rebuildList();
	applyFocus();

	ESP_LOGI(TAG, "MarketApp 初始化完成, server=%s", m_serverUrl.c_str());
}

// ============================================================
//  deinit / onForeground / onBackground
// ============================================================

void MarketApp::deinit()
{
	App::deinit();
}

void MarketApp::onForeground()
{
	App::onForeground();
	// 首次进入时自动拉取列表
	if (!m_hasFetched) {
		fetchList();
	}
	else if (!m_list.empty()) {
		// 返回市场时刷新已下载状态（可能刚下载过）
		refreshDownloadedState();
		updateActionLabels();
	}
}

void MarketApp::onBackground()
{
	App::onBackground();
}

// ============================================================
//  fetchList — 拉取脚本列表（异步）
// ============================================================

void MarketApp::fetchList()
{
	if (m_loading) return;
	m_loading = true;
	m_hasFetched = true;

	auto guard = display->lockGuard();
	if (guard) setStatus("连接服务器中...", GUI::Color::SUBTLE);

	std::string url = m_serverUrl;
	MarketClient::fetchList(url,
		[this](bool ok, std::vector<MarketScript> list, const std::string& err) {
			if (!running) return;   // App 已被销毁，放弃回调
			m_loading = false;
			auto g = display->lockGuard();
			if (!g) return;

			if (ok) {
				m_list = std::move(list);
				m_selectedIdx = m_list.empty() ? -1 : 0;
				refreshDownloadedState();
				rebuildList();
				if (m_list.empty())
					setStatus("服务器暂无脚本", GUI::Color::WARNING);
				else
					setStatus(("服务器: " + m_serverUrl).c_str(), GUI::Color::SUBTLE);
			}
			else {
				m_list.clear();
				rebuildList();
				std::string msg = "获取失败: " + err;
				if (m_serverUrl.empty())
					msg = "未配置服务器，请编辑 /root/system/market_config.json";
				setStatus(msg.c_str(), GUI::Color::DANGER);
			}
			applyFocus();
		});
}

// ============================================================
//  rebuildList — 重建网格卡片
// ============================================================

void MarketApp::rebuildList()
{
	// 清空现有卡片
	for (auto* c : m_entryCards) lv_obj_del(c);
	m_entryCards.clear();
	m_cardImages.clear();
	m_cardThPh.clear();

	// 重置缩略图状态
	m_thumbRequested.assign(m_list.size(), false);
	m_thumbLoaded.assign(m_list.size(), false);
	m_thumbQueue.clear();
	m_thumbLoading = false;

	auto* smlFont = FontLoader::getDefault(FontLoader::FontSize::Small);

	// 网格布局（flex 换行，3 列）
	lv_obj_set_layout(m_listContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_listContainer, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_style_pad_column(m_listContainer, 8, 0);
	lv_obj_set_style_pad_row(m_listContainer, 8, 0);
	lv_obj_set_style_flex_main_place(m_listContainer, LV_FLEX_ALIGN_START, 0);

	for (size_t i = 0; i < m_list.size(); i++) {
		const auto& s = m_list[i];

		auto* card = lv_obj_create(m_listContainer);
		lv_obj_set_size(card, lv_pct(32), 280);
		lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(0x18, 0x18, 0x28), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(card, 10, 0);
		lv_obj_set_style_border_width(card, 0, 0);
		lv_obj_set_style_pad_all(card, 8, 0);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_style_pad_gap(card, 6, 0);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);   // 卡片不可拖拽
		lv_obj_set_user_data(card, (void*)(uintptr_t)i);

		// 聚焦样式
		lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(card, LV_COLOR_MAKE(0x00, 0x88, 0xff), LV_STATE_FOCUSED);

		// 点击选中
		lv_obj_add_event_cb(card, [](lv_event_t* e) {
			auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
			lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
			int idx = (int)(uintptr_t)lv_obj_get_user_data(target);
			if (idx >= 0 && idx < (int)self->m_list.size()) {
				self->m_focusGroup = FOCUS_LIST;
				self->m_selectedIdx = (int8_t)idx;
				self->ensureVisibleThumbs();
				self->applyFocus();
			}
			}, LV_EVENT_CLICKED, this);

		// 截图区（未加载时显示评分星占位）
		auto* imgArea = lv_obj_create(card);
		lv_obj_set_size(imgArea, lv_pct(100), 172);
		lv_obj_set_style_border_width(imgArea, 1, 0);
		lv_obj_set_style_border_color(imgArea, LV_COLOR_MAKE(0x2a, 0x2a, 0x3a), 0);
		lv_obj_set_style_bg_color(imgArea, LV_COLOR_MAKE(0x0f, 0x0f, 0x1c), 0);
		lv_obj_set_style_bg_opa(imgArea, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(imgArea, 6, 0);
		lv_obj_set_style_clip_corner(imgArea, true, 0);
		lv_obj_set_style_pad_all(imgArea, 2, 0);
		lv_obj_clear_flag(imgArea, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(imgArea, LV_OBJ_FLAG_SCROLLABLE);   // 截图区不可拖拽

		auto* img = lv_image_create(imgArea);
		// 自适应：横屏缩略图按宽高比缩放到截图区
		lv_obj_set_size(img, lv_pct(100), lv_pct(100));
		lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);

		char phText[16];
		snprintf(phText, sizeof(phText), "★%.1f", s.rating);
		auto* ph = lv_label_create(imgArea);
		lv_label_set_text(ph, phText);
		lv_obj_set_style_text_color(ph, LV_COLOR_MAKE(0xff, 0xa8, 0x00), 0);
		lv_obj_set_style_text_font(ph, FontLoader::getDefault(FontLoader::FontSize::Medium), 0);
		lv_obj_center(ph);

		// 已下载角标（截图区右上角）
		if (i < m_downloaded.size() && m_downloaded[i]) {
			auto* badge = lv_label_create(imgArea);
			lv_label_set_text(badge, LV_SYMBOL_OK "已装");
			lv_obj_set_style_text_color(badge, GUI::Color::SUCCESS, 0);
			lv_obj_set_style_text_font(badge, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
			lv_obj_set_style_bg_color(badge, LV_COLOR_MAKE(0x0a, 0x4d, 0x2b), 0);
			lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
			lv_obj_set_style_pad_hor(badge, 4, 0);
			lv_obj_set_style_radius(badge, 4, 0);
			lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -2, 2);
		}

		// 名称
		auto* nameLbl = lv_label_create(card);
		lv_label_set_text(nameLbl, s.name.c_str());
		lv_obj_set_style_text_color(nameLbl, GUI::Color::TEXT, 0);
		lv_label_set_long_mode(nameLbl, LV_LABEL_LONG_DOT);
		lv_obj_set_width(nameLbl, lv_pct(100));

		// 作者 + 版本
		char sub[96];
		snprintf(sub, sizeof(sub), "%s  v%s",
			s.author.empty() ? "匿名" : s.author.c_str(),
			s.version.empty() ? "-" : s.version.c_str());
		auto* subLbl = lv_label_create(card);
		lv_label_set_text(subLbl, sub);
		lv_obj_set_style_text_color(subLbl, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(subLbl, smlFont, 0);

		m_entryCards.push_back(card);
		m_cardImages.push_back(img);
		m_cardThPh.push_back(ph);
	}

	// 预加载首屏缩略图
	int visible = GRID_COLS * 2;
	for (int i = 0; i < visible && i < (int)m_list.size(); i++)
		queueThumbnail(i);
}

// ============================================================
//  列表选择
// ============================================================

void MarketApp::selectNext()
{
	if (m_list.empty()) return;
	if (m_selectedIdx < (int)m_list.size() - 1) {
		m_selectedIdx++;
		ensureVisibleThumbs();
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

void MarketApp::selectPrev()
{
	if (m_list.empty()) return;
	if (m_selectedIdx > 0) {
		m_selectedIdx--;
		ensureVisibleThumbs();
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

void MarketApp::selectUp()
{
	if (m_list.empty()) return;
	if (m_selectedIdx >= GRID_COLS) {
		m_selectedIdx -= GRID_COLS;
		ensureVisibleThumbs();
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

void MarketApp::selectDown()
{
	if (m_list.empty()) return;
	if (m_selectedIdx + GRID_COLS < (int)m_list.size()) {
		m_selectedIdx += GRID_COLS;
		ensureVisibleThumbs();
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
	else {
		// 到最后一行按 ↓ 进入底部操作栏
		m_focusGroup = FOCUS_BOTTOM;
		m_focusBottomIdx = 0;
		auto guard = display->lockGuard();
		if (guard) applyFocus();
	}
}

// 预加载当前选中卡片及其邻居的缩略图
void MarketApp::ensureVisibleThumbs()
{
	if (m_selectedIdx < 0) return;
	queueThumbnail(m_selectedIdx);
	queueThumbnail(m_selectedIdx - 1);
	queueThumbnail(m_selectedIdx + 1);
	queueThumbnail(m_selectedIdx - GRID_COLS);
	queueThumbnail(m_selectedIdx + GRID_COLS);
}

// 请求加载某卡片缩略图（去重，入队）
void MarketApp::queueThumbnail(int idx)
{
	if (idx < 0 || idx >= (int)m_list.size()) return;
	if (m_thumbRequested[idx]) return;
	m_thumbRequested[idx] = true;
	if (m_list[idx].screenshot.empty() && m_list[idx].thumb.empty()) return;   // 无截图，保持评分星占位
	m_thumbQueue.push_back(idx);
	processThumbQueue();
}

// 串行处理缩略图下载队列（一次一个，避免并发任务爆炸）
void MarketApp::processThumbQueue()
{
	if (m_thumbLoading || m_thumbQueue.empty()) return;

	int idx = m_thumbQueue.front();
	m_thumbQueue.erase(m_thumbQueue.begin());
	m_thumbLoading = true;

	const MarketScript& s = m_list[idx];
	// 卡片优先用缩略图（小图，加载快），无缩略图则用大图
	const std::string& imgRel = !s.thumb.empty() ? s.thumb : s.screenshot;
	// 缓存文件名带作者命名空间（防不同作者同 id 游戏缩略图互相覆盖）
	std::string path = "/root/mem/market_" + sanitizeSegment(s.author) + "_" + s.id + "_thumb.jpg";
	std::string url = m_serverUrl;

	MarketClient::downloadFile(url, imgRel, path,
		[this, idx, path](bool ok, const std::string& /*err*/) {
			m_thumbLoading = false;
			if (!running) return;   // App 已销毁
			if (ok) {
				auto g = display->lockGuard();
				if (g && idx < (int)m_cardImages.size() && m_cardImages[idx]) {
					// /root/mem/market_x.jpg → F:mem/market_x.jpg
					std::string lvPath = "F:";
					size_t p = path.find("/root/");
					lvPath += (p != std::string::npos) ? path.substr(p + 6) : path;
					lv_image_set_src(m_cardImages[idx], lvPath.c_str());
					if (idx < (int)m_cardThPh.size() && m_cardThPh[idx])
						lv_obj_add_flag(m_cardThPh[idx], LV_OBJ_FLAG_HIDDEN);
					m_thumbLoaded[idx] = true;
				}
			}
			processThumbQueue();   // 继续加载下一个
		});
}

// ============================================================
//  焦点
// ============================================================

void MarketApp::applyFocus()
{
	for (auto* c : m_entryCards) lv_obj_clear_state(c, LV_STATE_FOCUSED);
	for (int i = 0; i < BOTTOM_BTN_COUNT; i++) lv_obj_clear_state(m_bottomBtns[i], LV_STATE_FOCUSED);
	if (m_detailBtnBack) lv_obj_clear_state(m_detailBtnBack, LV_STATE_FOCUSED);
	if (m_detailBtnRun) lv_obj_clear_state(m_detailBtnRun, LV_STATE_FOCUSED);

	switch (m_focusGroup) {
	case FOCUS_LIST:
		if (m_selectedIdx >= 0 && m_selectedIdx < (int)m_entryCards.size()) {
			lv_obj_add_state(m_entryCards[m_selectedIdx], LV_STATE_FOCUSED);
			lv_obj_scroll_to_view(m_entryCards[m_selectedIdx], LV_ANIM_OFF);
		}
		break;
	case FOCUS_BOTTOM:
		if (m_focusBottomIdx >= 0 && m_focusBottomIdx < BOTTOM_BTN_COUNT)
			lv_obj_add_state(m_bottomBtns[m_focusBottomIdx], LV_STATE_FOCUSED);
		break;
	case FOCUS_DETAIL:
		if (m_focusDetailIdx == 0 && m_detailBtnBack)
			lv_obj_add_state(m_detailBtnBack, LV_STATE_FOCUSED);
		else if (m_focusDetailIdx == 1 && m_detailBtnRun)
			lv_obj_add_state(m_detailBtnRun, LV_STATE_FOCUSED);
		break;
	}

	// 同步"运行 / 下载运行"按钮文案
	updateActionLabels();
}

// ============================================================
//  详情视图
// ============================================================

void MarketApp::showDetail()
{
	if (m_mode == MODE_DETAIL) return;
	if (m_list.empty() || m_selectedIdx < 0 || m_selectedIdx >= (int)m_list.size()) return;

	const auto& s = m_list[m_selectedIdx];
	m_mode = MODE_DETAIL;
	m_focusGroup = FOCUS_DETAIL;
	m_focusDetailIdx = 0;

	auto guard = display->lockGuard();
	if (!guard) return;

	// 全屏遮罩
	m_detailBox = lv_obj_create(screen);
	lv_obj_set_size(m_detailBox, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(m_detailBox, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(m_detailBox, LV_OPA_50, 0);
	lv_obj_set_style_border_width(m_detailBox, 0, 0);
	lv_obj_set_style_pad_all(m_detailBox, 0, 0);
	lv_obj_clear_flag(m_detailBox, LV_OBJ_FLAG_SCROLLABLE);   // 遮罩不可拖拽

	// 详情卡片
	m_detailCard = lv_obj_create(m_detailBox);
	lv_obj_set_size(m_detailCard, lv_pct(94), lv_pct(92));
	lv_obj_center(m_detailCard);
	lv_obj_set_style_bg_color(m_detailCard, LV_COLOR_MAKE(0x16, 0x16, 0x26), 0);
	lv_obj_set_style_radius(m_detailCard, 12, 0);
	lv_obj_set_style_pad_all(m_detailCard, 12, 0);
	lv_obj_set_flex_flow(m_detailCard, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_gap(m_detailCard, 12, 0);
	lv_obj_clear_flag(m_detailCard, LV_OBJ_FLAG_SCROLLABLE);   // 详情卡片不可拖拽

	// 左：截图区
	auto* imgArea = lv_obj_create(m_detailCard);
	lv_obj_set_size(imgArea, 360, lv_pct(100));
	lv_obj_set_style_border_width(imgArea, 1, 0);
	lv_obj_set_style_border_color(imgArea, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_bg_color(imgArea, LV_COLOR_MAKE(0x0f, 0x0f, 0x1c), 0);
	lv_obj_set_style_bg_opa(imgArea, LV_OPA_COVER, 0);
	lv_obj_set_style_clip_corner(imgArea, true, 0);
	lv_obj_set_style_pad_all(imgArea, 4, 0);
	lv_obj_clear_flag(imgArea, LV_OBJ_FLAG_SCROLLABLE);   // 详情截图区不可拖拽

	m_detailImage = lv_image_create(imgArea);		// 自适应：横屏截图 / 纵向拼接长图按宽高比缩放完整显示在槽位内
		lv_obj_set_size(m_detailImage, lv_pct(100), lv_pct(100));
		lv_image_set_inner_align(m_detailImage, LV_IMAGE_ALIGN_CONTAIN);	m_detailImagePh = lv_label_create(imgArea);
	lv_label_set_text(m_detailImagePh, "加载截图...");
	lv_obj_set_style_text_color(m_detailImagePh, GUI::Color::SUBTLE, 0);
	lv_obj_center(m_detailImagePh);

	// 右：信息区
	auto* right = lv_obj_create(m_detailCard);
	lv_obj_set_size(right, LV_SIZE_CONTENT, lv_pct(100));
	lv_obj_set_flex_grow(right, 1);
	lv_obj_set_style_border_width(right, 0, 0);
	lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_gap(right, 6, 0);

	auto* nameLabel = GUI::createTitle(right, s.name.c_str());
	lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_LEFT, 0);

	char meta[200];
	snprintf(meta, sizeof(meta), "作者: %s    版本: %s    评分: %.1f    下载: %d\n更新时间: %s",
		s.author.empty() ? "-" : s.author.c_str(),
		s.version.empty() ? "-" : s.version.c_str(),
		s.rating, s.downloads,
		s.updated.empty() ? "-" : s.updated.c_str());
	m_detailMeta = GUI::createLabel(right, meta);
	lv_obj_set_style_text_color(m_detailMeta, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_detailMeta, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	m_detailDesc = GUI::createLabel(right, s.desc.empty() ? "（无简介）" : s.desc.c_str());
	lv_obj_set_style_text_color(m_detailDesc, GUI::Color::TEXT, 0);
	lv_obj_set_width(m_detailDesc, lv_pct(100));
	lv_obj_set_flex_grow(m_detailDesc, 1);
	lv_label_set_long_mode(m_detailDesc, LV_LABEL_LONG_WRAP);

	// 按钮行
	auto* btnRow = lv_obj_create(right);
	lv_obj_set_size(btnRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_gap(btnRow, 8, 0);
	lv_obj_set_style_border_width(btnRow, 0, 0);
	lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);

	m_detailBtnBack = GUI::createButton(btnRow, "返回", 120, 40);
	lv_obj_set_style_radius(m_detailBtnBack, 6, 0);
	lv_obj_set_style_border_width(m_detailBtnBack, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_detailBtnBack, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_bg_color(m_detailBtnBack, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_add_event_cb(m_detailBtnBack, [](lv_event_t* e) {
		auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
		self->hideDetail();
		}, LV_EVENT_CLICKED, this);

	m_detailBtnRun = GUI::createButton(btnRow, "下载运行", 140, 40);
	lv_obj_set_style_radius(m_detailBtnRun, 6, 0);
	lv_obj_set_style_border_width(m_detailBtnRun, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_detailBtnRun, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_bg_color(m_detailBtnRun, LV_COLOR_MAKE(0x00, 0x66, 0xcc), 0);
	lv_obj_add_event_cb(m_detailBtnRun, [](lv_event_t* e) {
		auto* self = static_cast<MarketApp*>(lv_event_get_user_data(e));
		self->downloadAndRun();
		}, LV_EVENT_CLICKED, this);

	applyFocus();

	// 异步加载截图
	loadScreenshot(s);
}

void MarketApp::hideDetail()
{
	if (m_mode != MODE_DETAIL) return;
	m_mode = MODE_BROWSE;
	m_focusGroup = FOCUS_BOTTOM;
	m_focusDetailIdx = 0;

	auto guard = display->lockGuard();
	if (guard) {
		if (m_detailBox) lv_obj_del(m_detailBox);
		m_detailBox = nullptr;
		m_detailCard = nullptr;
		m_detailImage = nullptr;
		m_detailImagePh = nullptr;
		m_detailMeta = nullptr;
		m_detailDesc = nullptr;
		m_detailBtnBack = nullptr;
		m_detailBtnRun = nullptr;
		m_detailImageLoaded = false;
	}
	applyFocus();
}

// ============================================================
//  loadScreenshot — 下载并显示截图
// ============================================================

void MarketApp::loadScreenshot(const MarketScript& s)
{
	// 清理旧截图
	if (!m_detailImagePath.empty()) {
		removeFile(m_detailImagePath.c_str());
		m_detailImagePath.clear();
	}

	if (s.screenshot.empty() || m_serverUrl.empty()) {
		if (m_detailImagePh) lv_label_set_text(m_detailImagePh, "无截图");
		return;
	}

	// 截图缓存文件名带作者命名空间（防不同作者同 id 游戏截图互相覆盖）
	m_detailImagePath = "/root/mem/market_" + sanitizeSegment(s.author) + "_" + s.id + ".jpg";
	m_detailImageLoaded = false;

	std::string url = m_serverUrl;
	MarketClient::downloadFile(url, s.screenshot, m_detailImagePath,
		[this](bool ok, const std::string& /*err*/) {
			if (!running) return;   // App 已被销毁
			if (!ok) {
				auto g = display->lockGuard();
				if (g && m_detailImagePh) lv_label_set_text(m_detailImagePh, "截图加载失败");
				return;
			}
			auto g = display->lockGuard();
			if (!g || !m_detailImage) return;
			// 用 LVGL VFS 'F' 路径加载图片（/root/mem/market_x.jpg → F:mem/market_x.jpg）
			std::string lvPath = "F:";
			size_t p = m_detailImagePath.find("/root/");
			lvPath += (p != std::string::npos) ? m_detailImagePath.substr(p + 6)
			                                   : m_detailImagePath;
			lv_image_set_src(m_detailImage, lvPath.c_str());
			if (m_detailImagePh) lv_obj_add_flag(m_detailImagePh, LV_OBJ_FLAG_HIDDEN);
			m_detailImageLoaded = true;
		});
}

// ============================================================
//  downloadAndRun — 下载脚本并运行
// ============================================================

void MarketApp::downloadAndRun()
{
	if (m_downloading) return;
	if (m_list.empty() || m_selectedIdx < 0 || m_selectedIdx >= (int)m_list.size()) return;

	const auto& s = m_list[m_selectedIdx];
	int selIdx = m_selectedIdx;

	// 已下载 → 直接本地运行，不重复下载
	if (selIdx < (int)m_downloaded.size() && m_downloaded[selIdx]) {
		std::string path = localPathFor(s.author, s.id, s.download);
		ESP_LOGI(TAG, "本地运行: %s", path.c_str());
		if (m_manager) {
			m_manager->push(new LuaGameApp(display, path.c_str()));
		}
		return;
	}

	m_downloading = true;

	{
		auto guard = display->lockGuard();
		if (guard) setStatus("下载中...", GUI::Color::SUBTLE);
	}

	std::string url = m_serverUrl;
	MarketClient::downloadScriptText(url, s.download,
		[this, selIdx, id = s.id, author = s.author, download = s.download]
		(bool ok, const std::string& content, const std::string& err) {
			m_downloading = false;
			if (!running) return;   // App 已被销毁

			if (!ok) {
				auto g = display->lockGuard();
				if (g) setStatus(("下载失败: " + err).c_str(), GUI::Color::DANGER);
				return;
			}

			// 保存到 /root/lua/market/<作者>/<游戏>/（镜像服务器两级结构，作者/游戏隔离）
			std::string path = localPathFor(author, id, download);

			// 逐级建目录（mkdir 非递归）：/root/lua/market → <作者> → <游戏>
			newFloor("/root/lua/market");
			size_t lastSlash = path.rfind('/');
			if (lastSlash != std::string::npos) {
				std::string gameDir = path.substr(0, lastSlash);
				std::string authorDir = gameDir;
				size_t gameSlash = gameDir.rfind('/');
				if (gameSlash != std::string::npos) authorDir = gameDir.substr(0, gameSlash);
				newFloor(authorDir.c_str());
				newFloor(gameDir.c_str());
			}

			FILE* f = fopen(path.c_str(), "w");
			if (!f) {
				auto g = display->lockGuard();
				if (g) setStatus(("无法写入: " + path).c_str(), GUI::Color::DANGER);
				return;
			}
			size_t w = fwrite(content.c_str(), 1, content.size(), f);
			fclose(f);

			if (w != content.size()) {
				auto g = display->lockGuard();
				if (g) setStatus("写入不完整", GUI::Color::DANGER);
				return;
			}

			ESP_LOGI(TAG, "下载完成: %s (%zu bytes)", path.c_str(), content.size());

			// 标记已下载
			if (selIdx < (int)m_downloaded.size()) m_downloaded[selIdx] = true;

			// 启动游戏（回调在后台 Task，直接 push 安全）
			if (m_manager) {
				m_manager->push(new LuaGameApp(display, path.c_str()));
			}

			// 上报一次下载（尽力而为，后台 Task 执行，不阻塞）
			// 组合键 作者/id：服务端按组合键统计，防同名游戏串号
			MarketClient::reportDownload(m_serverUrl, author.empty() ? id : author + "/" + id);
		});
}

// ============================================================
//  路径与文件名净化
// ============================================================

// 路径段净化：保留 UTF-8（支持中文作者名），仅清洗 FAT/路径非法字符，拒绝 "." / ".."
std::string MarketApp::sanitizeSegment(const std::string& seg)
{
	if (seg == "." || seg == "..") return "";   // 拒绝路径穿越
	std::string out;
	out.reserve(seg.size());
	for (char c : seg) {
		if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
			c == '"' || c == '<' || c == '>' || c == '|' ||
			(unsigned char)c < 0x20) {
			out += '_';
		}
		else {
			out += c;
		}
	}
	if (out.size() > 128) out.resize(128);   // FAT 单段上限 255 字节，留余量
	if (out.empty()) out = "_";
	return out;
}

// 从下载 URL 提取市场内相对子路径（/games/<作者>/<游戏>/<文件> → <作者>/<游戏>/<文件>）
std::string MarketApp::localSubPathFor(const std::string& author, const std::string& id,
	const std::string& download)
{
	std::string rel;
	std::string dl = download;
	size_t g = dl.find("/games/");
	if (g != std::string::npos) {
		rel = dl.substr(g + 7);               // 去掉 "/games/" 前缀（与服务器目录镜像）
	}
	else {
		// 兜底：作者/游戏/文件名（如 download 缺省 /scripts/<id>.lua）
		std::string file = "game.lua";
		size_t slash = dl.rfind('/');
		if (slash != std::string::npos && slash + 1 < dl.size())
			file = dl.substr(slash + 1);
		if (file.empty()) file = "game.lua";
		rel = (author.empty() ? "unknown" : author) + "/"
			+ (id.empty() ? "game" : id) + "/" + file;
	}

	// 逐段净化（保留 UTF-8，拒绝路径穿越）
	std::string out;
	size_t pos = 0;
	while (pos <= rel.size()) {
		size_t s = rel.find('/', pos);
		std::string seg = rel.substr(pos, (s == std::string::npos) ? std::string::npos : s - pos);
		if (!seg.empty()) {
			std::string clean = sanitizeSegment(seg);
			if (!clean.empty()) {
				if (!out.empty()) out += "/";
				out += clean;
			}
		}
		if (s == std::string::npos) break;
		pos = s + 1;
	}
	if (out.empty()) out = "unknown/game/game.lua";
	return out;
}

// ============================================================
//  已下载状态
// ============================================================

// 脚本在本地市场目录的存储路径（/root/lua/market/<作者>/<游戏>/<文件>）
std::string MarketApp::localPathFor(const std::string& author, const std::string& id,
	const std::string& download)
{
	return "/root/lua/market/" + localSubPathFor(author, id, download);
}

bool MarketApp::isDownloaded(int idx) const
{
	if (idx < 0 || idx >= (int)m_downloaded.size()) return false;
	return m_downloaded[idx];
}

// 重新扫描本地市场目录，更新已下载标记
void MarketApp::refreshDownloadedState()
{
	m_downloaded.assign(m_list.size(), false);
	for (size_t i = 0; i < m_list.size(); i++) {
		const auto& s = m_list[i];
		m_downloaded[i] = testFile(localPathFor(s.author, s.id, s.download).c_str());
	}
}

// 同步"运行 / 下载运行"按钮文案（底部栏 + 详情）
void MarketApp::updateActionLabels()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	bool dl = (m_selectedIdx >= 0 && m_selectedIdx < (int)m_downloaded.size())
	          ? m_downloaded[m_selectedIdx] : false;

	auto setLabel = [](lv_obj_t* btn, const char* text) {
		if (btn) {
			lv_obj_t* lbl = lv_obj_get_child(btn, 0);
			if (lbl) lv_label_set_text(lbl, text);
		}
	};

	setLabel(m_bottomBtns[2], dl ? LV_SYMBOL_OK " 运行" : LV_SYMBOL_OK " 下载运行");
	setLabel(m_detailBtnRun, dl ? LV_SYMBOL_OK " 运行" : LV_SYMBOL_OK " 下载运行");
}

// ============================================================
//  手柄输入
// ============================================================

void MarketApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	TickType_t now = xTaskGetTickCount();

	// 边沿检测
	uint16_t newPress = state.buttons & ~m_prevButtons[hostId][playerId];
	m_prevButtons[hostId][playerId] = state.buttons;

	// 摇杆导航（独立延迟）
	bool hasMove = lxLeft || lxRight || lyUp || lyDown;
	if (!hasMove)
		m_nextMoveTime[hostId][playerId] = 0;
	else if (m_nextMoveTime[hostId][playerId] >= now)
		hasMove = false;
	else {
		TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
		m_nextMoveTime[hostId][playerId] = now + delay;
	}

	// 按钮动作全局去重
	if (!newPress && now < m_nextActionTime) return;

	switch (m_mode) {
	case MODE_BROWSE:
	{
		switch (m_focusGroup) {
		case FOCUS_LIST:
		{
			if (hasMove && lyUp)
				selectUp();
			else if (hasMove && lyDown)
				selectDown();
			else if (hasMove && lxLeft)
				selectPrev();
			else if (hasMove && lxRight)
				selectNext();
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)
				|| newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)) {
				m_nextActionTime = now + ACTION_COOLDOWN;
				showDetail();
			}
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
				m_nextActionTime = now + ACTION_COOLDOWN;
				popApp();
			}
			break;
		}
		case FOCUS_BOTTOM:
		{
			if (hasMove && lxLeft) {
				if (m_focusBottomIdx > 0) {
					m_focusBottomIdx--;
					auto guard = display->lockGuard();
					if (guard) applyFocus();
				}
			}
			else if (hasMove && lxRight) {
				if (m_focusBottomIdx < BOTTOM_BTN_COUNT - 1) {
					m_focusBottomIdx++;
					auto guard = display->lockGuard();
					if (guard) applyFocus();
				}
			}
			else if (hasMove && lyUp) {
				m_focusGroup = FOCUS_LIST;
				auto guard = display->lockGuard();
				if (guard) applyFocus();
			}
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) {
				m_nextActionTime = now + ACTION_COOLDOWN;
				switch (m_focusBottomIdx) {
				case 0: fetchList(); break;
				case 1: showDetail(); break;
				case 2: downloadAndRun(); break;
				}
			}
			else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
				m_nextActionTime = now + ACTION_COOLDOWN;
				popApp();
			}
			break;
		}
		case FOCUS_DETAIL: break;
		}
		break;
	}

	case MODE_DETAIL:
	{
		if (hasMove && lxLeft && m_focusDetailIdx > 0) {
			m_focusDetailIdx--;
			auto guard = display->lockGuard();
			if (guard) applyFocus();
		}
		else if (hasMove && lxRight && m_focusDetailIdx < 1) {
			m_focusDetailIdx++;
			auto guard = display->lockGuard();
			if (guard) applyFocus();
		}
		else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) {
			m_nextActionTime = now + ACTION_COOLDOWN;
			if (m_focusDetailIdx == 1)
				downloadAndRun();
			else
				hideDetail();
		}
		else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
			m_nextActionTime = now + ACTION_COOLDOWN;
			hideDetail();
		}
		break;
	}
	}
}

// ============================================================
//  AI 能力
// ============================================================

std::string MarketApp::getAiContext()
{
	std::string ctx = "当前在游戏市场。\n"
		"可用焦点组（select 的 text 参数）：\n"
		"- list(0-N)\t脚本列表\n"
		"- actions(0-2)\t操作按钮（刷新/详情/下载运行）\n"
		"- detail(0-1)\t详情按钮（返回/下载运行）\n\n";

	ctx += "服务器: " + m_serverUrl + "\n";

	if (m_list.empty()) {
		ctx += "列表为空（可能未连接或未加载）。\n";
	}
	else {
		for (size_t i = 0; i < m_list.size(); i++) {
			const auto& s = m_list[i];
			ctx += "  " + std::to_string(i) + ": " + s.name
				+ " (" + s.author + ", ★" + std::to_string((int)s.rating) + ")\n";
		}
	}

	ctx += "\n选中索引: " + std::to_string(m_selectedIdx) + "\n";
	return ctx;
}

void MarketApp::onAiSelect(const AiSelectTarget& target)
{
	auto guard = display->lockGuard();
	if (!guard) return;

	if (target.group == "list") {
		m_focusGroup = FOCUS_LIST;
		if (target.index >= 0 && target.index < (int)m_list.size()) {
			m_selectedIdx = (int8_t)target.index;
		}
		applyFocus();
	}
	else if (target.group == "actions") {
		m_focusGroup = FOCUS_BOTTOM;
		if (target.index >= 0 && target.index < BOTTOM_BTN_COUNT) {
			m_focusBottomIdx = (int8_t)target.index;
		}
		applyFocus();
	}
	else if (target.group == "detail") {
		if (m_mode != MODE_DETAIL) showDetail();
		m_focusGroup = FOCUS_DETAIL;
		if (target.index == 0 || target.index == 1) {
			m_focusDetailIdx = (int8_t)target.index;
		}
		applyFocus();
	}
}

void MarketApp::onAiActivate()
{
	switch (m_focusGroup) {
	case FOCUS_LIST:
		showDetail();
		break;
	case FOCUS_BOTTOM:
		switch (m_focusBottomIdx) {
		case 0: fetchList(); break;
		case 1: showDetail(); break;
		case 2: downloadAndRun(); break;
		}
		break;
	case FOCUS_DETAIL:
		if (m_focusDetailIdx == 1)
			downloadAndRun();
		else
			hideDetail();
		break;
	}
}
