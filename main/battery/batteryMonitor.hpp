#pragma once

#include <cstdint>
#include <string>

#include "display/display.hpp"
#include "gamepad/gamepadState.hpp"
#include "audio/Audio.hpp"
#include "task/task.hpp"
#include "mutex/mutex.hpp"

/**
 * @brief 低电量检测提醒（主机 + 蓝牙手柄）
 *
 * 系统级单例：周期 Task 轮询主机 BatteryManager 与各手柄槽位 BleGamepad，
 * 单阈值穿越判定（主机 <15%、手柄 <20%，连续 2 次确认防抖），触发
 * lv_layer_sys() 系统级弹窗（列出所有低电量设备）+ 提示音。
 *
 * 语义：
 * - 仅提醒一次（按设备）：每个设备的每个低电事件只提醒一次，充电 / 电量回升 /
 *   手柄断连后该设备复位，下次再次掉电可重新提醒；用户关闭只影响当前低电设备，
 *   其他设备后续掉电仍会触发提醒。
 * - 弹窗关闭：触摸 [知道了] 按钮，或任意手柄按键（边沿检测）。
 * - 输入屏蔽：弹窗显示期间（isAlertShown()）手柄输入不再分发到底层 App。
 * - 提示音：AudioHandle 持有循环播放 /root/system/music/alert.aac（提高警示度），
 *   关闭弹窗时 `alertAudio = {}`（移动赋值）停止；文件缺失时静默降级为纯视觉弹窗。
 *
 * 使用方式（main.cpp，BleGamepad::start() 与 AiAgentOverlay::start() 之后）：
 * @code
 *   BatteryMonitor::instance().start(&display);
 * @endcode
 */
class BatteryMonitor
{
public:
	static BatteryMonitor& instance();

	/** @brief 初始化：建弹窗 UI（初始隐藏）+ 启动监测 Task + 手柄关闭定时器 */
	bool start(Display* display);

	/** @brief 停止监测并释放 UI（一般无需调用） */
	void stop();

	/** @brief 弹窗当前是否显示（VirtualGamepad 分发时据此屏蔽底层 App 输入） */
	bool isAlertShown() const { return m_alertShown; }

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
	static constexpr char      kAlertSoundPath[] = "/root/system/music/alert.aac"; // 提示音

	// ── 弹窗 UI（lv_layer_sys()） ──
	bool buildUi();                 // 须持 LVGL 锁调用
	void showAlertLocked();         // 须持 LVGL 锁调用
	void hideAlert(bool userDismiss = true); // 须持 LVGL 锁调用（定时器/按钮回调在 LVGL 线程内可直接调）

	// ── 监测 Task ──
	static TickType_t monitorTaskCb(void* arg);
	void tick();                    // 每周期：读电量 → 状态机 → 弹窗控制

	// ── 手柄按键关闭弹窗（LVGL 定时器，边沿检测） ──
	static void dismissTimerCb(lv_timer_t* t);

	// ── 提示音 ──
	void playAlertSound();          // Audio::play() 即用即弃播放（勿持 LVGL 锁调用）

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

	// ── 弹窗状态 ──
	bool        m_alertShown{};
	std::string m_bodyText;        // 当前弹窗设备列表文本（用于去重 set_text）
	Mutex       m_alertMutex;      // ⚠ 保护 alertAudio 与状态机（monitor task ↔ LVGL task 并发）
	AudioHandle alertAudio{};

	// ── LVGL 对象（建在 lv_layer_sys() 上） ──
	lv_obj_t* m_scrim{};           // 全屏半透明遮罩（拦截底层触摸）
	lv_obj_t* m_card{};            // 居中弹窗卡片
	lv_obj_t* m_titleLabel{};
	lv_obj_t* m_bodyLabel{};       // 设备列表多行文本
	lv_obj_t* m_okBtn{};

	// ── 手柄关闭 ──
	lv_timer_t* m_dismissTimer{};
	uint16_t    m_prevButtons[MaxPlayers]{};   // 上一帧各手柄按键（边沿基线）
};
