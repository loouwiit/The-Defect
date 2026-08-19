#include "gui/systemPopup.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "gamepad/virtualGamepad.hpp"

#include "esp_log.h"

static constexpr char TAG[] = "SystemPopup";

// ════════════════════════════════════════════════════════════════
// 单例
// ════════════════════════════════════════════════════════════════

SystemPopup& SystemPopup::instance()
{
	static SystemPopup s_instance{};
	return s_instance;
}

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

bool SystemPopup::start(Display* display)
{
	if (m_started) return true;
	m_display = display;

	// ── 建弹窗 UI（持锁） ──
	{
		auto guard = display->lockGuard();
		if (!guard)
		{
			ESP_LOGE(TAG, "无法锁定显示，初始化失败");
			return false;
		}
		if (!buildUi())
		{
			ESP_LOGE(TAG, "弹窗 UI 构建失败");
			return false;
		}
	}

	// ── 手柄按键关闭定时器（LVGL 线程内，边沿检测） ──
	m_dismissTimer = lv_timer_create([](lv_timer_t* t) {
		SystemPopup::dismissTimerCb(t);
		}, 100, this);

	m_started = true;
	ESP_LOGI(TAG, "系统弹窗服务已启动");
	return true;
}

void SystemPopup::stop()
{
	if (m_dismissTimer)
	{
		lv_timer_del(m_dismissTimer);
		m_dismissTimer = nullptr;
	}

	Lock lock{ m_mutex };

	stopAlertSound();

	if (m_started && m_display)
	{
		if (auto guard = m_display->lockGuard())
		{
			if (m_scrim) lv_obj_delete(m_scrim);
			if (m_card)  lv_obj_delete(m_card);
			m_scrim = nullptr;
			m_card = nullptr;
		}
	}

	m_shown.store(false, std::memory_order_relaxed);
	m_currentValid = false;
	m_queue.clear();
	m_started = false;
	ESP_LOGI(TAG, "已停止");
}

// ════════════════════════════════════════════════════════════════
// 弹窗 UI（移植自 BatteryMonitor::buildUi）
// ════════════════════════════════════════════════════════════════

bool SystemPopup::buildUi()
{
	auto* sysLayer = lv_layer_sys();
	auto* medFont = FontLoader::getDefault(FontLoader::FontSize::Default); // 32px
	auto* smlFont = FontLoader::getDefault(FontLoader::FontSize::Small);   // 24px

	// ── 全屏半透明遮罩（拦截底层触摸，突出弹窗） ──
	m_scrim = lv_obj_create(sysLayer);
	lv_obj_set_size(m_scrim, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(m_scrim, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(m_scrim, LV_OPA_40, 0);
	lv_obj_set_style_border_width(m_scrim, 0, 0);
	lv_obj_set_style_radius(m_scrim, 0, 0);
	lv_obj_set_style_pad_all(m_scrim, 0, 0);
	lv_obj_clear_flag(m_scrim, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(m_scrim, LV_OBJ_FLAG_CLICK_FOCUSABLE);
	lv_obj_add_flag(m_scrim, LV_OBJ_FLAG_CLICKABLE);   // 吞掉底层触摸
	lv_obj_add_flag(m_scrim, LV_OBJ_FLAG_HIDDEN);

	// ── 居中弹窗卡片 ──
	m_card = lv_obj_create(sysLayer);
	lv_obj_set_size(m_card, 520, LV_SIZE_CONTENT);
	lv_obj_center(m_card);
	lv_obj_set_style_bg_color(m_card, GUI::Color::CARD, 0);
	lv_obj_set_style_bg_opa(m_card, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(m_card, 16, 0);
	lv_obj_set_style_border_width(m_card, 2, 0);
	lv_obj_set_style_border_color(m_card, GUI::Color::PRIMARY, 0);   // 默认色，showCurrent 时更新
	lv_obj_set_style_shadow_width(m_card, 30, 0);
	lv_obj_set_style_shadow_color(m_card, lv_color_black(), 0);
	lv_obj_set_style_shadow_opa(m_card, LV_OPA_60, 0);
	lv_obj_set_style_pad_all(m_card, 24, 0);
	lv_obj_set_flex_flow(m_card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(m_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(m_card, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(m_card, LV_OBJ_FLAG_CLICK_FOCUSABLE);
	lv_obj_add_flag(m_card, LV_OBJ_FLAG_HIDDEN);

	// 标题
	m_titleLabel = lv_label_create(m_card);
	lv_label_set_text(m_titleLabel, "");
	lv_obj_set_style_text_font(m_titleLabel, medFont, 0);
	lv_obj_set_style_text_color(m_titleLabel, GUI::Color::PRIMARY, 0); // 默认色，showCurrent 时更新

	// 正文（多行）
	m_bodyLabel = lv_label_create(m_card);
	lv_label_set_text(m_bodyLabel, "");
	lv_obj_set_width(m_bodyLabel, 460);
	lv_label_set_long_mode(m_bodyLabel, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(m_bodyLabel, smlFont, 0);
	lv_obj_set_style_text_color(m_bodyLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_align(m_bodyLabel, LV_TEXT_ALIGN_CENTER, 0);

	// 可选提示行（默认隐藏，有 footerText 时显示）
	m_footerLabel = lv_label_create(m_card);
	lv_label_set_text(m_footerLabel, "");
	lv_obj_set_style_text_font(m_footerLabel, smlFont, 0);
	lv_obj_set_style_text_color(m_footerLabel, GUI::Color::SUBTLE, 0);
	lv_obj_add_flag(m_footerLabel, LV_OBJ_FLAG_HIDDEN);

	// [知道了] 按钮
	m_okBtn = GUI::createButton(m_card, "知道了", 220, 52);
	lv_obj_set_style_bg_color(m_okBtn, GUI::Color::PRIMARY, 0);
	lv_obj_set_style_text_font(lv_obj_get_child(m_okBtn, 0), smlFont, 0);
	lv_obj_add_event_cb(m_okBtn, [](lv_event_t* e) {
		auto* self = static_cast<SystemPopup*>(lv_event_get_user_data(e));
		self->hideCurrent(true);
		}, LV_EVENT_CLICKED, this);

	return true;
}

// ════════════════════════════════════════════════════════════════
// show / hide
// ════════════════════════════════════════════════════════════════

void SystemPopup::show(Key key, const char* title, const char* body, const Options& opts)
{
	Lock lock{ m_mutex };

	// 已在显示中且同 key → 原地更新内容
	if (m_currentValid && m_current.key == key)
	{
		m_current.title = title ? title : "";
		m_current.body = body ? body : "";
		m_current.opts = opts;

		// 更新 UI（持 LVGL 锁）
		if (auto guard = m_display->lockGuard())
		{
			m_bodyCache = m_current.body;
			lv_label_set_text(m_bodyLabel, m_bodyCache.c_str());
			lv_obj_set_style_border_color(m_card, m_current.opts.accent, 0);
			lv_obj_set_style_text_color(m_titleLabel, m_current.opts.accent, 0);
		}
		ESP_LOGD(TAG, "弹窗 [%s] 已更新内容", key);
		return;
	}

	// 已在队列中且同 key → 更新排队项（不改变队列位置）
	for (size_t i = 0; i < m_queue.size(); i++)
	{
		if (m_queue[i].key == key)
		{
			m_queue[i].title = title ? title : "";
			m_queue[i].body = body ? body : "";
			m_queue[i].opts = opts;
			ESP_LOGD(TAG, "弹窗 [%s] 队列项已更新", key);
			return;
		}
	}

	// 当前无弹窗 → 直接显示
	if (!m_currentValid)
	{
		m_current.key = key;
		m_current.title = title ? title : "";
		m_current.body = body ? body : "";
		m_current.opts = opts;
		m_currentValid = true;

		if (auto guard = m_display->lockGuard())
			showCurrent();

		ESP_LOGI(TAG, "弹窗 [%s] 显示: %s", key, title ? title : "");
		return;
	}

	// 当前有弹窗 → 排队（FIFO）
	if (m_queue.size() < kMaxQueue)
	{
		PopupReq req;
		req.key = key;
		req.title = title ? title : "";
		req.body = body ? body : "";
		req.opts = opts;
		m_queue.push_back(std::move(req));
		ESP_LOGI(TAG, "弹窗 [%s] 已排队（位置 %zu/%zu）", key, m_queue.size(), kMaxQueue);
	}
	else
	{
		ESP_LOGW(TAG, "弹窗 [%s] 排队已满（%zu），已丢弃", key, kMaxQueue);
	}
}

void SystemPopup::hide(Key key, bool userDismiss)
{
	Lock lock{ m_mutex };

	if (!m_currentValid) return;

	if (m_current.key == key)
	{
		if (auto guard = m_display->lockGuard())
			hideCurrent(userDismiss);
		return;
	}

	// key 不匹配 → 从队列中移除
	for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
	{
		if (it->key == key)
		{
			ESP_LOGI(TAG, "弹窗 [%s] 从队列移除", key);
			m_queue.erase(it);
			return;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 弹窗显示/隐藏（须持 LVGL 锁）
// ════════════════════════════════════════════════════════════════

void SystemPopup::showCurrent()
{
	if (!m_currentValid) return;

	// 更新标题
	lv_label_set_text(m_titleLabel, m_current.title.c_str());
	lv_obj_set_style_text_color(m_titleLabel, m_current.opts.accent, 0);
	lv_obj_set_style_border_color(m_card, m_current.opts.accent, 0);

	// 更新正文
	m_bodyCache = m_current.body;
	lv_label_set_text(m_bodyLabel, m_bodyCache.c_str());

	// 更新提示行
	if (m_current.opts.footerText && m_current.opts.footerText[0] != '\0')
	{
		lv_label_set_text(m_footerLabel, m_current.opts.footerText);
		lv_obj_clear_flag(m_footerLabel, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_add_flag(m_footerLabel, LV_OBJ_FLAG_HIDDEN);
	}

	// 更新按钮文字
	lv_label_set_text(lv_obj_get_child(m_okBtn, 0), m_current.opts.buttonText);

	// 显示弹窗
	lv_obj_clear_flag(m_scrim, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_card, LV_OBJ_FLAG_HIDDEN);
	m_shown.store(true, std::memory_order_relaxed);

	// 复位手柄关闭边沿基线：以当前按键状态为基线，避免按住中的按键立即关掉弹窗
	for (int p = 0; p < MaxPlayers; p++)
		m_prevButtons[p] = VirtualGamepad::instance().getState(0, p).buttons;

	// 播放提示音
	if (m_current.opts.playAlertSound) {
		m_alertAudio = Audio::play("/root/system/music/alert.aac");
		m_alertAudio.setLoop(true);
	}
}

void SystemPopup::hideCurrent(bool userDismiss)
{
	if (!m_currentValid) return;

	stopAlertSound();

	// 触发关闭回调（仅用户操作时）
	if (userDismiss && m_current.opts.onDismiss)
		m_current.opts.onDismiss();

	m_shown.store(false, std::memory_order_relaxed);
	lv_obj_add_flag(m_scrim, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_card, LV_OBJ_FLAG_HIDDEN);

	ESP_LOGI(TAG, "弹窗 [%s] 关闭%s", m_current.key, userDismiss ? "（用户操作）" : "");

	m_currentValid = false;

	// 推进队列：显示下一个排队弹窗
	advanceQueue();
}

void SystemPopup::advanceQueue()
{
	if (m_queue.empty()) return;

	// 弹出队首
	m_current = std::move(m_queue.front());
	m_queue.pop_front();
	m_currentValid = true;

	ESP_LOGI(TAG, "弹窗 [%s] 从队列显示（剩余 %zu）", m_current.key, m_queue.size());

	if (auto guard = m_display->lockGuard())
		showCurrent();
}

// ════════════════════════════════════════════════════════════════
// 手柄按键关闭弹窗（LVGL 定时器，边沿检测）
// ════════════════════════════════════════════════════════════════

void SystemPopup::dismissTimerCb(lv_timer_t* t)
{
	auto* self = static_cast<SystemPopup*>(lv_timer_get_user_data(t));
	if (!self || !self->m_started || !self->m_shown.load(std::memory_order_relaxed)) return;

	// 任意本机手柄任意新按键 → 关闭弹窗
	for (uint8_t p = 0; p < MaxPlayers; p++)
	{
		uint16_t cur = VirtualGamepad::instance().getState(0, p).buttons;
		uint16_t edge = cur & ~self->m_prevButtons[p];
		self->m_prevButtons[p] = cur;
		if (edge)
		{
			Lock lock{ self->m_mutex };
			if (self->m_currentValid)
				self->hideCurrent(true);
			return;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 提示音
// ════════════════════════════════════════════════════════════════

void SystemPopup::stopAlertSound()
{
	m_alertAudio = {};   // 移动赋值 → 旧 Impl 析构 → stop()
}
