#pragma once

#include "app/app.hpp"
#include "discovery/discovery.hpp"
#include "host/hostManager.hpp"
#include "lvgl.h"
#include <vector>
#include <cstdint>

/**
 * @brief 多主机管理 App
 *
 * 通过 mDNS 发现局域网内其他游戏主机，展示列表并提供连接入口。
 * 自动定期扫描，支持手动刷新。
 */
class ConsoleManagerApp final : public App
{
public:
	constexpr static char TAG[] = "ConsoleManagerApp";

	ConsoleManagerApp(Display* display);
	~ConsoleManagerApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(int8_t hostId, int8_t playerId, const GamepadState& state) override;

private:
	static constexpr int MaxConsoles = 16;
	static constexpr int ScanTimeoutMs = 3000;
	static constexpr int RefreshInterval = 5000;  // 自动刷新间隔（ms）
	static constexpr int RowHight = 64;

	// ── UI 控件 ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_scanBtn{};
	lv_obj_t* m_scanBtnLabel{};
	lv_obj_t* m_scanListContainer{};
	lv_obj_t* m_statusLabel{};

	// ── 连接状态 UI ──
	lv_obj_t* m_connectedContainer{};   // 连接状态区域（从机视图）
	lv_obj_t* m_connectedLabel{};       // "已连接到 [hostname]"
	lv_obj_t* m_disconnectBtn{};        // "断开"按钮
	bool m_connectedView{ false };      // 当前是否为从机视图

	// ── 扫描结果 ──
	std::vector<DiscoveredConsole> m_consoles;
	bool m_scanActive{ false };

	// ── 槽位 UI（4 个设备槽 + 掩码按钮）──
	lv_obj_t* m_slotsContainer{};
	lv_obj_t* m_slotNameLabels[4]{};
	lv_obj_t* m_maskBtns[4][4]{};   // [slot][playerId]
	int8_t m_swapSelectedSlot{ -1 };

	// ── 列表行 ──
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};

	// ── 手柄焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,  // 返回 / 扫描
		FOCUS_LIST,       // 主机列表
		FOCUS_SLOTS,      // 设备槽位
		FOCUS_MASK,       // 掩码按钮
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };   // 0=返回, 1=扫描
	int8_t m_focusListIdx{ 0 };
	int8_t m_focusSlotIdx{ 0 };
	int8_t m_focusMaskSlotIdx{ 0 };
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 内部方法 ──
	void buildUi();
	void buildSlots();
	void refreshUi();
	void updateScanList();
	void updateStatus();
	void doScan();
	void doConnect(size_t index);
	void disconnectFromHost();
	void switchToConnectedView();
	void switchToScanView();
	void updateSlots();
	void updateMaskBtnVisual(int slot);
	void doSwapSlots(int slotA, int slotB);

	void applyFocus();
	void activateFocus();
	void navTitleLeft();
	void navTitleRight();
	void navListUp();
	void navListDown();
	void navSlotLeft();
	void navSlotRight();
	void navSlotUp();
	void navSlotDown();

	// ── 静态回调 ──
	static void timerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);
	static void onDisconnectBtnCb(lv_event_t* e);
	static void onSlotClickCb(lv_event_t* e);
	static void onMaskBtnCb(lv_event_t* e);

	// ── AI 能力 ──
	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;
};
