#pragma once

#include "app/app.hpp"
#include "lvgl.h"
#include <esp_wifi_types_generic.h>
#include <vector>
#include <cstdint>

/**
 * @brief WiFi 设置 App
 *
 * 提供扫描、连接、断开 WiFi 的图形界面。
 *
 * 扫描策略：
 * - 用户点"扫描"→ 在 Task 中执行同步扫描 → 列表更新
 * - 选择 AP → 弹出密码键盘 → 确认后连接
 * - 底部显示当前连接状态
 */
class WifiSettingsApp final : public App
{
public:
	constexpr static char TAG[] = "WifiSettingsApp";

	WifiSettingsApp(Display* display);
	~WifiSettingsApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	static constexpr int MaxAps = 32;
	static constexpr int ScanDuration = 10000;
	static constexpr int RefreshInterval = 1000;
	static constexpr int RowHight = 60;

	// ── 扫描控制 ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_scanBtn{};
	lv_obj_t* m_scanBtnLabel{};
	bool m_scanActive{ false };
	TickType_t m_scanStartTick{};

	// ── WiFi 开关 ──
	lv_obj_t* m_wifiToggleBtn{};
	lv_obj_t* m_wifiToggleLabel{};

	// ── 扫描结果（本地冻结副本） ──
	std::vector<wifi_ap_record_t> m_scanResults;

	// ── UI 容器 ──
	lv_obj_t* m_scanListContainer{};
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	// ── 连接信息区域 ──
	lv_obj_t* m_infoCard{};
	lv_obj_t* m_infoIcon{};
	lv_obj_t* m_infoSsidLabel{};
	lv_obj_t* m_infoIpLabel{};
	lv_obj_t* m_disconnectBtn{};
	bool m_isConnected{ false };

	// ── 密码弹窗 ──
	lv_obj_t* m_passwordDialog{};
	lv_obj_t* m_passwordTa{};
	lv_obj_t* m_keyboard{};
	size_t m_pwdTargetIdx{ 0 };

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};

	// ── 手柄焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,  // 左右: 返回 ↔ 扫描
		FOCUS_LIST,       // 上下: 设备列表
		FOCUS_INFO,       // 底部: 连接信息卡 + 断开按钮
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };   // 0=返回, 1=扫描
	int8_t m_focusListIdx{ 0 };    // 0..N-1 AP 列表
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 内部方法 ──
	void buildUi();
	void refreshUi();
	void updateScanList();
	void updateConnectionInfo();
	void doScan();
	void doConnect(size_t scanIndex, const char* password);
	void doDisconnect();
	void toggleWifi();
	void showPasswordDialog(size_t scanIndex);
	void hidePasswordDialog();
	void applyFocus();
	void activateFocus();
	void navTitleLeft();
	void navTitleRight();
	void navListUp();
	void navListDown();

	// ── 回调（静态，C 兼容） ──
	static void timerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onWifiToggleCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onDisconnectBtnCb(lv_event_t* e);
	static void onPwdConfirmCb(lv_event_t* e);
	static void onPwdCancelCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);
};
