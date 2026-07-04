#pragma once

#include "app/app.hpp"
#include "bleGamepad/bleGamepad.hpp"
#include "lvgl.h"
#include <vector>
#include <cstdint>

/**
 * @brief BLE 手柄设置 App
 *
 * 提供扫描、连接、断开、删除 BLE 手柄的图形界面。
 *
 * 扫描策略（方案 A）：
 * - 进入 App 时 stopScan()，清空本地列表
 * - 用户点"扫描"→ startScan() 扫 5 秒 → 自动 stopScan()，列表冻结
 * - 列表冻结后摇杆/触控操作无干扰
 * - 离开 App 时 startScan() 恢复后台持续扫描
 */
class BleSettingsApp final : public App
{
public:
	constexpr static char TAG[] = "BleSettingsApp";

	BleSettingsApp(Display* display);
	~BleSettingsApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;

	// BLE 任务通知（仅打标记，不操作 LVGL）
	void onGamepadConnected(uint8_t playerId) override;
	void onGamepadDisconnected(uint8_t playerId) override;

	// ── 手柄控制 ──
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	static constexpr int ScanDuration = 5000;
	static constexpr int RefreshInterval = 1000;
	static constexpr int SlotHight = 100;
	static constexpr int RowHight = 60;

	// ── 扫描控制 ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_scanBtn{};
	lv_obj_t* m_scanBtnLabel{};
	bool m_scanActive{ false };
	TickType_t m_scanStartTick{ 0 };

	// ── 扫描结果（本地冻结副本，扫描停止后不再变化） ──
	std::vector<ScanDevice> m_localScanResults;

	// ── UI 容器 ──
	lv_obj_t* m_scanListContainer{};
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	// ── 已连接区域 ──
	lv_obj_t* m_connectedContainer{};
	lv_obj_t* m_slotCards[MaxPlayers]{};
	lv_obj_t* m_slotLabels[MaxPlayers]{};
	lv_obj_t* m_slotInfoLabels[MaxPlayers]{};
	lv_obj_t* m_disconnectBtns[MaxPlayers]{};
	lv_obj_t* m_moveBtns[MaxPlayers]{};   // 移动按钮
	bool m_slotConnected[MaxPlayers]{};
	lv_obj_t* m_saveBtn{};           // 保存配对按钮

	// ── 移动模式 ──
	bool m_moveMode{ false };
	int8_t m_moveSourceIdx{ -1 };

	// ── 活动指示 ──
	TickType_t m_lastActivityTime[MaxPlayers]{};
	bool m_lastActivityStatus[MaxPlayers]{};
	static constexpr TickType_t ACTIVITY_TIMEOUT = pdMS_TO_TICKS(100);
	static constexpr TickType_t ACTIVITY_REFRESH_MS = 20;

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};
	lv_timer_t* m_restoreTimer{};   // 保存按钮视觉反馈恢复定时器
	lv_timer_t* m_activityTimer{};  // 活动指示快速刷新定时器

	// ── 标记位（BLE 任务设置，timer 消费） ──
	bool m_pendingRefresh{ false };

	// ── 手柄焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,  // 左右: 返回 ↔ 扫描
		FOCUS_LIST,       // 上下: 设备列表, 左右到列表首尾
		FOCUS_SLOTS,      // 左右: 4 个槽位, 上下跳到列表/标题
		FOCUS_CARD_BTNS,  // 卡片内部按钮: ⇄ ↔ 断开（上退回 FOCUS_SLOTS）
		FOCUS_SAVE,       // 保存按钮（从 P4 右进，左回 P4，上回扫描）
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };   // 0=返回, 1=扫描
	int8_t m_focusListIdx{ 0 };    // 0..N-1 设备列表
	int8_t m_focusSlotsIdx{ 0 };   // 0..3 槽位
	int8_t m_focusBtnIdx{ 0 };     // 0=移动按钮, 1=断开按钮（FOCUS_CARD_BTNS 内）
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 回调（静态，C 兼容） ──
	static void timerCb(lv_timer_t* t);
	static void activityTimerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onDisconnectBtnCb(lv_event_t* e);
	static void onDeleteBtnCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);
	static void onSaveBtnCb(lv_event_t* e);
	static void onMoveBtnCb(lv_event_t* e);

	// ── 内部方法 ──
	void buildUi();
	void refreshUi();
	void refreshActivityIndicators();
	void updateScanList();
	void updateConnectedList();
	void toggleScan();
	void doConnect(size_t scanIndex);
	void doDisconnect(uint8_t playerId);
	void doDelete(size_t scanIndex);
	void doMove(uint8_t from, uint8_t to);
	void cancelMoveMode();
	void applyFocus();
	void activateFocus();
	void navTitleLeft();
	void navTitleRight();
	void navListUp();
	void navListDown();
	void navListHome();
	void navListEnd();
	void navSlotsLeft();
	void navSlotsRight();
};
