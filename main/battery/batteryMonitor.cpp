#include "battery/batteryMonitor.hpp"
#include "battery/batteryManager.hpp"
#include "gamepad/bleGamepad.hpp"
#include "gui/gui.hpp"
#include "gui/systemPopup.hpp"

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
	m_started = false;
	ESP_LOGI(TAG, "已停止");
}

// ════════════════════════════════════════════════════════════════
// 监测 Task（UI 由 SystemPopup 管理）
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

	// 是否有设备在本轮新触发（用于播放提示音）
	bool newlyAlerted = false;

	// ⚠ 状态机数据段持 m_mutex（monitor Task ↔ onDismiss 回调并发）
	std::string body;
	bool anyActive = false;
	{
		Lock lock{ m_mutex };

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
		anyActive = !body.empty();
	}   // 锁释放

	// ── 4. 弹窗控制 ──
	if (anyActive)
	{
		if (body != m_bodyText)
		{
			m_bodyText = body;

			SystemPopup::Options opts;
			opts.accent = GUI::Color::DANGER;
			opts.footerText = "请及时充电，充电后提示自动消除";
			opts.playAlertSound = newlyAlerted;
			opts.onDismiss = [this]() {
				// 用户关闭：仅标记当前待提醒设备为"已关闭"
				Lock lock{ m_mutex };
				if (m_host.alerted && m_host.low) m_host.dismissed = true;
				for (int i = 0; i < MaxPlayers; i++)
					if (m_pad[i].alerted && m_pad[i].low) m_pad[i].dismissed = true;
			};
			SystemPopup::instance().show(SystemPopup::kBattery,
				LV_SYMBOL_WARNING " 电量不足", m_bodyText.c_str(), opts);
		}
	}
	else
	{
		// 无待提醒设备（均已关闭或恢复）→ 自动隐藏；各源状态保留至恢复后复位
		// userDismiss=false 不触发 onDismiss（电量恢复不是用户操作）
		SystemPopup::instance().hide(SystemPopup::kBattery, false);
	}
}
