#pragma once

#include "app/app.hpp"
#include "bleGamepad/bleGamepad.hpp"
#include "lvgl.h"
#include <cstdint>

/**
 * @brief 电源管理 App
 *
 * 显示 ESP32-P4 主机电量及已连接手柄的电量，
 * 提供关机（Deep-sleep）和低功耗模式（Light-sleep）入口。
 *
 * 电量颜色规则：
 *   - >60% 绿色
 *   - 20%~60% 黄色
 *   - <20% 红色
 *
 * 电池读取（使用 adc_battery_estimation 组件）：
 *   依赖 `espressif/adc_battery_estimation` 组件，
 *   ADC 通道和分压电阻参数根据实际硬件配置。
 */
class PowerManagementApp final : public App
{
public:
	constexpr static char TAG[] = "PowerManagementApp";

	PowerManagementApp(Display* display);
	~PowerManagementApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;

	// 手柄控制
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;
	void onGamepadConnected(uint8_t playerId) override;
	void onGamepadDisconnected(uint8_t playerId) override;

private:
	// ── 常量 ──
	static constexpr uint8_t MaxPlayers = 4;
	static constexpr int RefreshIntervalMs = 2000;     // 电量刷新间隔
	static constexpr int ActivityRefreshMs = 20;       // 活动指示刷新间隔
	static constexpr TickType_t ActivityTimeout = pdMS_TO_TICKS(100);

	static constexpr int BatteryGreenMin = 61;   // 60%+ 绿色
	static constexpr int BatteryYellowMin = 21;  // 20%~60% 黄色
	                                         // <20% 红色

	static constexpr int HostBatteryCardH = 260;
	static constexpr int SlotCardH = 100;

	// ── UI 对象 ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_titleLabel{};

	// 主机电池区域
	lv_obj_t* m_hostCard{};
	lv_obj_t* m_hostIconLabel{};
	lv_obj_t* m_hostPercentLabel{};
	lv_obj_t* m_hostBar{};
	lv_obj_t* m_hostVoltageLabel{};

	// 手柄电池槽位
	lv_obj_t* m_slotCards[MaxPlayers]{};
	lv_obj_t* m_slotLabels[MaxPlayers]{};
	lv_obj_t* m_slotBars[MaxPlayers]{};
	lv_obj_t* m_slotPercentLabels[MaxPlayers]{};

	// 操作按钮
	lv_obj_t* m_shutdownBtn{};
	lv_obj_t* m_shutdownBtnLabel{};
	lv_obj_t* m_lowPowerBtn{};
	lv_obj_t* m_lowPowerBtnLabel{};

	// ── 数据 ──
	int m_hostBatteryPercent{ 0 };     // 0~100
	int m_hostVoltageMv{ 0 };          // mV
	bool m_slotConnected[MaxPlayers]{};
	TickType_t m_lastActivityTime[MaxPlayers]{};
	bool m_lastActivityStatus[MaxPlayers]{};

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};
	lv_timer_t* m_activityTimer{};
	lv_timer_t* m_restoreTimer{};      // 按钮文字恢复定时器

	// ── 焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,       // 左右: 返回
		FOCUS_SLOTS,           // 左右: 4 个手柄槽位
		FOCUS_BUTTONS,         // 左右: 关机 ↔ 低功耗
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusBtnIdx{ 0 };       // 0=关机, 1=低功耗
	int8_t m_focusSlotsIdx{ 0 };     // 0..3 槽位
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};

	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;
	static constexpr TickType_t ACTION_DELAY = 500;

	// ── 电池读取 ──
	bool initBatteryAdc();
	int readHostBatteryPercent();
	int readHostVoltageMv();

	// ── 低功耗模式状态 ──
	bool mLowPowerActive{ false };

	// ── 内部方法 ──
	void buildUi();
	void refreshBatteryUi();
	void refreshSlotUi();
	void refreshActivityIndicators();

	void doShutdown();
	void doToggleLowPower();
	void applyFocus();
	void activateFocus();

	static lv_color_t batteryColor(int percent);

	// ── 静态回调 ──
	static void timerCb(lv_timer_t* t);
	static void activityTimerCb(lv_timer_t* t);
	static void onBackBtnCb(lv_event_t* e);
	static void onShutdownBtnCb(lv_event_t* e);
	static void onLowPowerBtnCb(lv_event_t* e);
};
