#pragma once

#include "app/app.hpp"
#include "lvgl.h"
#include <cstdint>

/**
 * @brief 时间设置 App
 *
 * 提供手动调整时间、WiFi 网络时间同步（SNTP）的图形界面。
 *
 * 功能：
 * - 大字体显示当前时间
 * - 手动调整时/分/秒
 * - 自动网络时间同步开关（WiFi 连接后自动同步）
 * - 立即同步按钮（仅 WiFi 连接时可用）
 * - WiFi 状态指示
 */
class TimeSettingsApp final : public App
{
public:
	constexpr static char TAG[] = "TimeSettingsApp";

	TimeSettingsApp(Display* display);
	~TimeSettingsApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	// ── UI 控件 ──
	lv_obj_t* m_backBtn{};

	// 时间显示区域
	lv_obj_t* m_timeLabel{};          // 大号 HH:MM:SS
	lv_obj_t* m_dateLabel{};          // 小号 YYYY-MM-DD

	// 手动调整按钮
	lv_obj_t* m_hourDownBtn{};
	lv_obj_t* m_hourUpBtn{};
	lv_obj_t* m_hourValueLabel{};
	lv_obj_t* m_minDownBtn{};
	lv_obj_t* m_minUpBtn{};
	lv_obj_t* m_minValueLabel{};
	lv_obj_t* m_secResetBtn{};
	lv_obj_t* m_secValueLabel{};

	// 同步控制
	lv_obj_t* m_autoSyncToggle{};     // 开关按钮
	lv_obj_t* m_autoSyncToggleLabel{}; // 开关按钮的文字标签
	lv_obj_t* m_autoSyncLabel{};
	lv_obj_t* m_syncNowBtn{};
	lv_obj_t* m_wifiStatusLabel{};
	lv_obj_t* m_syncStatusLabel{};

	// SNTP 状态
	bool m_autoSync{ false };
	bool m_syncInProgress{ false };
	TickType_t m_syncStartTick{ 0 };
	static constexpr int SyncTimeout = 15000; // 15s SNTP 超时

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};
	static constexpr int RefreshInterval = 500; // 500ms 刷新时间显示

	// ── 手柄焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,   // 返回按钮
		FOCUS_ADJUST,      // 时/分/秒调整按钮行
		FOCUS_SYNC,        // 同步控制行
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };    // 0=返回
	int8_t m_focusAdjustIdx{ 0 };   // 0=时−, 1=时+, 2=分−, 3=分+, 4=秒↺
	int8_t m_focusSyncIdx{ 0 };     // 0=自动同步, 1=立即同步
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};

	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;
	static constexpr TickType_t ACTION_DELAY = 500;

	// ── 内部方法 ──
	void buildUi();
	void updateTimeDisplay();
	void refreshUi();
	void applyFocus();
	void activateFocus();

	void adjustHour(int delta);
	void adjustMinute(int delta);
	void resetSeconds();
	void toggleAutoSync();
	void doSyncNow();

	// ── 静态回调 ──
	static void timerCb(lv_timer_t* t);
	static void onBackBtnCb(lv_event_t* e);
	static void onHourDownCb(lv_event_t* e);
	static void onHourUpCb(lv_event_t* e);
	static void onMinDownCb(lv_event_t* e);
	static void onMinUpCb(lv_event_t* e);
	static void onSecResetCb(lv_event_t* e);
	static void onAutoSyncCb(lv_event_t* e);
	static void onSyncNowCb(lv_event_t* e);
};
