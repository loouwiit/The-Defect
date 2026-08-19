#pragma once

#include <cstdint>
#include <string>

#include "display/display.hpp"
#include "gamepad/gamepadState.hpp"
#include "task/task.hpp"
#include "mutex/mutex.hpp"

/**
 * @brief 低电量检测提醒（主机 + 蓝牙手柄）
 *
 * 系统级单例：周期 Task 轮询主机 BatteryManager 与各手柄槽位 BleGamepad，
 * 单阈值穿越判定（主机 <20%、手柄 <20%，连续 2 次确认防抖），通过
 * SystemPopup 弹出系统级弹窗（列出所有低电量设备）+ 循环提示音。
 *
 * 语义：
 * - 仅提醒一次（按设备）：每个设备的每个低电事件只提醒一次，充电 / 电量回升 /
 *   手柄断连后该设备复位，下次再次掉电可重新提醒；用户关闭只影响当前低电设备，
 *   其他设备后续掉电仍会触发提醒。
 * - 弹窗关闭：触摸 [知道了] 按钮，或任意手柄按键（边沿检测，由 SystemPopup 处理）。
 * - 输入屏蔽：弹窗显示期间（SystemPopup::isShown()）手柄输入不再分发到底层 App。
 * - 提示音：由 SystemPopup 统一管理（playAlertSound=true），关闭弹窗时自动停止。
 *
 * 使用方式（main.cpp）：
 * @code
 *   BatteryMonitor::instance().start(&display);   // 建监测 Task
 * @endcode
 */
class BatteryMonitor
{
public:
	static BatteryMonitor& instance();

	/** @brief 启动监测 Task（UI 由 SystemPopup 管理） */
	bool start(Display* display);

	/** @brief 停止监测（一般无需调用） */
	void stop();

private:
	BatteryMonitor() = default;
	~BatteryMonitor() = default;
	BatteryMonitor(const BatteryMonitor&) = delete;
	BatteryMonitor& operator=(const BatteryMonitor&) = delete;

	// ── 阈值与节奏（可调） ──
	static constexpr int       kHostLowPercent  = 20;                  // 主机低电量阈值
	static constexpr int       kPadLowPercent   = 20;                  // 手柄低电量阈值
	static constexpr TickType_t kPollIntervalMs = 15000;               // 监测周期
	static constexpr int       kConfirmTicks    = 2;                   // 连续 N 次低于阈值才触发（防抖）

	// ── 监测 Task ──
	static TickType_t monitorTaskCb(void* arg);
	void tick();                    // 每周期：读电量 → 状态机 → SystemPopup 弹窗控制

	// ── 依赖 ──
	Display* m_display{};
	bool     m_started{};
	Task*    m_task{};

	// ── 每源低电状态机 ──
	struct SourceState {
		bool low{ false };         // 当前低于阈值（主机还需非充电）
		bool alerted{ false };     // 已提醒（每个低电事件只提醒一次）
		bool dismissed{ false };   // 用户已关闭该设备提醒（仅影响本设备，恢复后复位）
		int  confirmTicks{ 0 };    // 连续低于阈值次数
	};
	SourceState m_host{};
	SourceState m_pad[MaxPlayers]{};

	// ── 弹窗去重 ──
	std::string m_bodyText;        // 当前弹窗设备列表文本（用于去重 show 更新）
	Mutex       m_mutex;           // ⚠ 保护状态机（monitor task ↔ SystemPopup onDismiss 回调并发）
};
