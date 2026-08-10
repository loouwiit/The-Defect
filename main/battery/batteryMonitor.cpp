#include "battery/batteryMonitor.hpp"
#include "battery/batteryManager.hpp"
#include "gamepad/bleGamepad.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"

#include "esp_log.h"

static constexpr char TAG[] = "BatteryMonitor";

// ════════════════════════════════════════════════════════════════
// 单例
// ════════════════════════════════════════════════════════════════

BatteryMonitor& BatteryMonitor::instance()
{
	static BatteryMonitor s_instance{};
	return s_instance;
}

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

bool BatteryMonitor::start(Display* display)
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
		BatteryMonitor::dismissTimerCb(t);
		}, 100, this);

	// ── 监测 Task（独立 daemon 线程，避免阻塞 LVGL） ──
	m_task = Task::addTask(monitorTaskCb, "batteryMonitor", this, 0, Task::Affinity::None);

	m_started = true;
	ESP_LOGI(TAG, "低电量监测已启动 (主机<%d%% / 手柄<%d%%, 周期%lums)",
		kHostLowPercent, kPadLowPercent, (unsigned long)kPollIntervalMs);
	return true;
}

void BatteryMonitor::stop()
{
	if (m_task)
	{
		Task::removeTask(m_task);
		m_task = nullptr;
	}
	if (m_dismissTimer)
	{
		lv_timer_del(m_dismissTimer);
		m_dismissTimer = nullptr;
	}
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
	m_started = false;
	ESP_LOGI(TAG, "已停止");
}

// ════════════════════════════════════════════════════════════════
// 弹窗 UI
// ════════════════════════════════════════════════════════════════

bool BatteryMonitor::buildUi()
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
	lv_obj_set_style_border_color(m_card, GUI::Color::DANGER, 0);
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
	lv_label_set_text(m_titleLabel, LV_SYMBOL_WARNING " 电量不足");
	lv_obj_set_style_text_font(m_titleLabel, medFont, 0);
	lv_obj_set_style_text_color(m_titleLabel, GUI::Color::DANGER, 0);

	// 设备列表（多行）
	m_bodyLabel = lv_label_create(m_card);
	lv_label_set_text(m_bodyLabel, "");
	lv_obj_set_width(m_bodyLabel, 460);
	lv_label_set_long_mode(m_bodyLabel, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(m_bodyLabel, smlFont, 0);
	lv_obj_set_style_text_color(m_bodyLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_align(m_bodyLabel, LV_TEXT_ALIGN_CENTER, 0);

	// 提示行
	auto* hint = lv_label_create(m_card);
	lv_label_set_text(hint, "请及时充电，充电后提示自动消除");
	lv_obj_set_style_text_font(hint, smlFont, 0);
	lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);

	// [知道了] 按钮
	m_okBtn = GUI::createButton(m_card, "知道了", 220, 52);
	lv_obj_set_style_bg_color(m_okBtn, GUI::Color::PRIMARY, 0);
	lv_obj_set_style_text_font(lv_obj_get_child(m_okBtn, 0), smlFont, 0);
	lv_obj_add_event_cb(m_okBtn, [](lv_event_t* e) {
		auto* self = static_cast<BatteryMonitor*>(lv_event_get_user_data(e));
		self->hideAlert();
		}, LV_EVENT_CLICKED, this);

	return true;
}

void BatteryMonitor::showAlertLocked()
{
	if (m_alertShown) return;

	m_alertShown = true;
	lv_obj_clear_flag(m_scrim, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_card, LV_OBJ_FLAG_HIDDEN);

	// 复位手柄关闭边沿基线：以当前按键状态为基线，避免按住中的按键立即关掉弹窗
	for (int p = 0; p < MaxPlayers; p++)
		m_prevButtons[p] = VirtualGamepad::instance().getState(0, p).buttons;

	ESP_LOGI(TAG, "低电量弹窗显示");
}

void BatteryMonitor::hideAlert(bool userDismiss)
{
	if (!m_alertShown) return;

	if (userDismiss)
	{
		// 用户关闭：仅标记当前待提醒设备为“已关闭”（按设备限制，其他设备后续掉电仍可提醒）
		Lock lock{ m_alertMutex };
		if (m_host.alerted && m_host.low) m_host.dismissed = true;
		for (int i = 0; i < MaxPlayers; i++)
			if (m_pad[i].alerted && m_pad[i].low) m_pad[i].dismissed = true;

		alertAudio = {};
	}
	else
	{
		Lock lock{ m_alertMutex };
		alertAudio = {};
	}
	m_alertShown = false;
	lv_obj_add_flag(m_scrim, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_card, LV_OBJ_FLAG_HIDDEN);

	ESP_LOGI(TAG, "低电量弹窗关闭%s", userDismiss ? "（用户操作）" : "（无待提醒）");
}

// ════════════════════════════════════════════════════════════════
// 监测 Task
// ════════════════════════════════════════════════════════════════

TickType_t BatteryMonitor::monitorTaskCb(void* arg)
{
	static_cast<BatteryMonitor*>(arg)->tick();
	return pdMS_TO_TICKS(kPollIntervalMs);
}

void BatteryMonitor::tick()
{
	// ── 1. 读主机电量（无锁，纯数据读取） ──
	int hostPct = BatteryManager::instance().getPercent();
	bool hostCharging = BatteryManager::instance().isCharging();
	bool hostLow = (hostPct >= 0) && !hostCharging && (hostPct < kHostLowPercent);

	// 是否有设备在本轮新触发（用于播放提示音 + 再次弹窗）
	bool newlyAlerted = false;

	// 更新主机状态机
	if (hostLow)
	{
		m_host.low = true;
		m_host.confirmTicks++;
		if (!m_host.alerted && m_host.confirmTicks >= kConfirmTicks)
		{
			m_host.alerted = true;
			newlyAlerted = true;
			ESP_LOGI(TAG, "主机低电量触发 (%d%%)", hostPct);
		}
	}
	else
	{
		if (m_host.alerted || m_host.low || m_host.dismissed)
			ESP_LOGI(TAG, "主机电量恢复/充电中 (%d%%)，低电状态复位", hostPct);
		m_host = {};
	}

	// ── 2. 读各手柄电量 ──
	for (int i = 0; i < MaxPlayers; i++)
	{
		const auto* dev = BleGamepad::instance().getDevice(i);
		if (!dev || !dev->connected)
		{
			if (m_pad[i].alerted || m_pad[i].low || m_pad[i].dismissed)
				ESP_LOGI(TAG, "手柄 P%d 断开，低电状态复位", i + 1);
			m_pad[i] = {};
			continue;
		}

		uint8_t level = dev->batteryLevel;
		bool padLow = (level != 255) && (level < kPadLowPercent);  // 255=未知，跳过
		if (padLow)
		{
			m_pad[i].low = true;
			m_pad[i].confirmTicks++;
			if (!m_pad[i].alerted && m_pad[i].confirmTicks >= kConfirmTicks)
			{
				m_pad[i].alerted = true;
				newlyAlerted = true;
				ESP_LOGI(TAG, "手柄 P%d (%s) 低电量触发 (%d%%)", i + 1,
					dev->name[0] ? dev->name : "?", level);
			}
		}
		else
		{
			if (m_pad[i].alerted || m_pad[i].low || m_pad[i].dismissed)
				ESP_LOGI(TAG, "手柄 P%d 电量恢复 (%d%%)，低电状态复位", i + 1, level);
			m_pad[i] = {};
		}
	}

	// ── 3. 汇总待提醒设备行（低电 && 已触发 && 未被用户关闭） ──
	std::string body;
	if (m_host.alerted && m_host.low && !m_host.dismissed)
	{
		char line[64];
		snprintf(line, sizeof(line), "主机 电量 %d%%", hostPct);
		body += line;
		body += "\n";
	}
	for (int i = 0; i < MaxPlayers; i++)
	{
		if (!(m_pad[i].alerted && m_pad[i].low && !m_pad[i].dismissed)) continue;
		const auto* dev = BleGamepad::instance().getDevice(i);
		char line[96];
		snprintf(line, sizeof(line), "手柄 P%d (%s) 电量 %d%%", i + 1,
			(dev && dev->name[0]) ? dev->name : "?",
			(dev && dev->batteryLevel <= 100) ? dev->batteryLevel : 0);
		body += line;
		body += "\n";
	}
	bool anyActive = !body.empty();

	// ── 4. 提示音：新设备触发时播放 ──
	if (newlyAlerted)
		playAlertSound();

	// ── 5. 弹窗控制（持锁） ──
	auto guard = m_display->lockGuard();
	if (!guard)
	{
		ESP_LOGW(TAG, "无法锁定显示，跳过本轮弹窗更新");
		return;
	}

	if (anyActive)
	{
		if (body != m_bodyText)
		{
			m_bodyText = body;
			lv_label_set_text(m_bodyLabel, m_bodyText.c_str());
		}
		showAlertLocked();
	}
	else
	{
		// 无待提醒设备（均已关闭或恢复）→ 自动隐藏；各源状态保留至恢复后复位
		if (m_alertShown) hideAlert(false);
	}
}

// ════════════════════════════════════════════════════════════════
// 手柄按键关闭弹窗（LVGL 定时器，边沿检测）
// ════════════════════════════════════════════════════════════════

void BatteryMonitor::dismissTimerCb(lv_timer_t* t)
{
	auto* self = static_cast<BatteryMonitor*>(lv_timer_get_user_data(t));
	if (!self || !self->m_started || !self->m_alertShown) return;

	// 任意本机手柄任意新按键 → 关闭弹窗
	for (uint8_t p = 0; p < MaxPlayers; p++)
	{
		uint16_t cur = VirtualGamepad::instance().getState(0, p).buttons;
		uint16_t edge = cur & ~self->m_prevButtons[p];
		self->m_prevButtons[p] = cur;
		if (edge)
		{
			self->hideAlert();
			return;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 提示音
// ════════════════════════════════════════════════════════════════

void BatteryMonitor::playAlertSound()
{
	// ⚠ monitor task 与 LVGL task（hideAlert 停止提示音）并发操作 alertAudio，
	// 须加锁（移动赋值是 swap 两个 shared_ptr，并发 = 双重释放）
	Lock lock{ m_alertMutex };
	// Audio::play() 对文件缺失等失败路径返回空句柄，detach() 自动为 no-op
	alertAudio = Audio::play(kAlertSoundPath).setLoop(true);
	alertAudio.play();
}
