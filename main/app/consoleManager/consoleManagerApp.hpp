#pragma once

#include "app/app.hpp"
#include "discovery/discovery.hpp"
#include "wifi/wsClient.hpp"
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
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

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

	// ── 扫描结果 ──
	std::vector<DiscoveredConsole> m_consoles;
	bool m_scanActive{ false };

	// ── WS 测试连接 ──
	WsClient* m_testClient{};
	int m_testSeq{};

	// ── 列表行 ──
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};

	// ── 手柄焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,  // 返回 / 扫描
		FOCUS_LIST,       // 主机列表
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusTitleIdx{ 0 };   // 0=返回, 1=扫描
	int8_t m_focusListIdx{ 0 };
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 内部方法 ──
	void buildUi();
	void refreshUi();
	void updateScanList();
	void updateStatus();
	void doScan();
	void doConnect(size_t index);

	void applyFocus();
	void activateFocus();
	void navTitleLeft();
	void navTitleRight();
	void navListUp();
	void navListDown();

	// ── 静态回调 ──
	static void timerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);

	// ── AI 能力 ──
	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;
};
