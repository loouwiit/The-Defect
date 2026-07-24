#pragma once

#include "app/app.hpp"
#include "discovery/discovery.hpp"
#include "host/hostManager.hpp"
#include "lvgl.h"
#include <vector>
#include <cstdint>

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
	static constexpr int RowHight = 64;
	static constexpr int SLOT_COUNT = 4;

	lv_obj_t* m_backBtn{};
	lv_obj_t* m_scanBtn{};
	lv_obj_t* m_scanBtnLabel{};
	lv_obj_t* m_scanListContainer{};

	lv_obj_t* m_slotContainer{};
	lv_obj_t* m_slotCards[SLOT_COUNT]{};
	lv_obj_t* m_slotLabels[SLOT_COUNT]{};
	lv_obj_t* m_maskBtns[SLOT_COUNT][4]{};
	lv_obj_t* m_connStatusLabel{};
	lv_obj_t* m_swapBtns[SLOT_COUNT]{};
	lv_obj_t* m_disconnectBtns[SLOT_COUNT]{};

	std::vector<DiscoveredConsole> m_consoles;
	bool m_scanActive{ false };
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	lv_timer_t* m_refreshTimer{};
	int8_t m_swapSelected = -1;

	enum FocusGroup : int8_t {
		FOCUS_TITLE, FOCUS_LIST, FOCUS_SLOTS, FOCUS_MASK, FOCUS_CARD_BTNS,
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };
	int8_t m_focusListIdx{ 0 };
	int8_t m_focusSlotIdx{ 0 };
	int8_t m_focusMaskIdx{ 0 };
	int8_t m_focusActionIdx{ 0 };
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	void buildUi();
	void buildSlots();
	void refreshUi();
	void updateScanList();
	void updateSlots();
	void updateStatus();
	void doScan();
	void doConnect(size_t index);
	void toggleMask(int slotId, int playerId);
	void applyFocus();
	void activateFocus();
	void navLeft();
	void navRight();
	void navUp();
	void navDown();

	static void timerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);
	static void onSlotClickCb(lv_event_t* e);
	static void onMaskClickCb(lv_event_t* e);
	static void onSlotSwapCb(lv_event_t* e);
	static void onSlotDisconnectCb(lv_event_t* e);

	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;
};
