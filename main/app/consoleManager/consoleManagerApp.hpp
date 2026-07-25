#pragma once

#include "app/app.hpp"
#include "discovery/discovery.hpp"
#include "host/hostManager.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "lvgl.h"
#include <vector>
#include <cstdint>

/**
 * @brief 多主机管理 App
 *
 * BLE 配对风格界面：
 *   - 扫描区域：发现局域网内其他主机
 *   - 4 槽位区域：每槽显示主机名 + P0-P3 掩码按钮 + 交换/断开
 *   - 主机/从机双视图切换
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
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	static constexpr int MaxConsoles = 16;
	static constexpr int ScanTimeoutMs = 3000;
	static constexpr int RefreshInterval = 5000;
	static constexpr int RowHight = 60;
	static constexpr int SlotHight = 180;
	static constexpr int MASK_BTN_SIZE = 28;

	// ── 扫描结果 ──
	std::vector<DiscoveredConsole> m_consoles;
	bool m_scanActive{ false };

	// ── 视图容器 ──
	lv_obj_t* m_hostView{};
	lv_obj_t* m_clientView{};

	// ── 主机视图：顶栏 ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_scanBtn{};
	lv_obj_t* m_scanBtnLabel{};

	// ── 主机视图：扫描列表 ──
	lv_obj_t* m_scanListContainer{};
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	// ── 主机视图：4 槽位 ──
	struct SlotWidgets {
		lv_obj_t* card{};
		lv_obj_t* nameLabel{};
		lv_obj_t* maskBtns[4]{};
		lv_obj_t* swapBtn{};
		lv_obj_t* disconnectBtn{};
	};
	SlotWidgets m_slotsUI[4]{};
	bool m_maskState[4][4]{};         // [hostId][playerId] = 启用

	// ── 交换模式 ──
	bool   m_swapMode{ false };
	int8_t m_swapSourceSlot{ -1 };

	// ── 从机视图 ──
	lv_obj_t* m_connectedInfoLabel{};
	lv_obj_t* m_disconnectBtn{};
	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};

	// ── 焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE,
		FOCUS_LIST,
		FOCUS_SLOTS,
		FOCUS_CLIENT,   // 从机视图：返回/断开
	};
	enum SlotNavLevel : int8_t {
		SLOT_CARD,    // 选哪个槽位 (←→ 切换)
		SLOT_MASK,    // 掩码按钮网格
		SLOT_ACTIONS, // 交换/断开按钮
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };
	int8_t m_focusListIdx{ 0 };
	int8_t m_focusClientIdx{ 0 };   // 从机视图：0=返回, 1=断开
	int8_t m_focusSlotIdx{ 0 };        // 当前聚焦的槽位 (0-3)
	int8_t m_focusMaskCol{ 0 };         // 掩码列
	int8_t m_focusActionIdx{ 0 };       // 动作按钮索引 (0=交换, 1=断开)
	SlotNavLevel m_slotNavLevel{ SLOT_CARD };
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 活动指示 ──
	TickType_t m_lastActivityTime[MaxHosts][MaxPlayers]{};
	bool      m_lastActivityState[4]{};
	lv_timer_t* m_activityTimer{};
	static constexpr TickType_t ACTIVITY_TIMEOUT = pdMS_TO_TICKS(100);
	static constexpr TickType_t ACTIVITY_REFRESH_MS = 20;

	// ── 内部方法 ──
	void buildHostView();
	void buildClientView();
	void applyViewMode();
	void buildTopBar();
	void buildSlotCards();
	void refreshUi();
	void updateScanList();
	void doScan();
	void doConnect(size_t index);
	void updateSlotStatus();
	void updateSlotCard(int slot, const HostManager::ClientSlot& info);
	void updateMaskBtnStyle(int hostId, int player);
	void applyMaskToVirtualGamepad();
	void refreshActivityIndicators();
	void onSlotClick(int slot);
	void cancelSwapMode();
	void onSwapSlot(int slotA, int slotB);
	void onDisconnectSlot(int slot);

	void applyFocus();
	void activateFocus();
	void navUp();
	void navDown();
	void navLeft();
	void navRight();

	// ── 静态回调 ──
	static void timerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);
	static void onMaskBtnCb(lv_event_t* e);
	static void onSwapBtnCb(lv_event_t* e);
	static void onDisconnectBtnCb(lv_event_t* e);

	// ── AI 能力 ──
	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;
};
