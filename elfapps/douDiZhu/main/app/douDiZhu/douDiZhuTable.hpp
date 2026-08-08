#pragma once

#include "lvgl.h"
#include <cstdint>
#include <functional>
#include "display/display.hpp"
#include "douDiZhuLogic.hpp"

/**
 * @brief 斗地主牌桌渲染器（1280×720 横屏）
 *
 * 自持全部 LVGL 对象池（手牌/出牌区/牌背/底牌/按钮），
 * 通过 render(State) 增量刷新。只渲染本机手牌，对手仅牌背 + 出牌。
 *
 * 布局：
 *   左上=下一家(seat+1)，右上=上一家(seat+2)，底部=本机手牌
 *   顶部中央=底牌，右侧=出牌/不出/提示按钮，叫分时中部弹叫分按钮
 */
class DouDizhuTable
{
public:
	struct State
	{
		int mySeat = 0;
		int phase = 0;            // 0=等待 1=叫分 2=出牌 3=结算

		uint8_t hand[DouDizhuLogic::MAX_HAND]{};
		int handCount = 0;

		int bidTurn = -1;
		int minBid = 1;
		int maxBid = 0;
		int maxBidder = -1;
		int dealer = 0;

		int turn = -1;
		int landlord = -1;
		int bidValue = 0;
		int countdownSec = 30;

		uint8_t trick[3][DouDizhuLogic::MAX_HAND]{};
		int trickCount[3]{};
		bool trickPass[3]{};
		int cardCount[3]{};

		bool myMustLead = false;
		bool myCanPass = false;

		uint8_t bottom[DouDizhuLogic::BOTTOM_SIZE]{};
		bool bottomRevealed = false;

		int scores[3]{};
		int delta[3]{};
		int multiplier = 1;
		bool landlordWin = false;
		bool spring = false;
		bool reverseSpring = false;

		char names[3][64]{};
		bool connected = true;
	};

	// ── 回调（App 设置） ──
	std::function<void(int bidValue)> onBid;    // 1..3, 0=不叫
	std::function<void()> onPlay;
	std::function<void()> onPass;
	std::function<void()> onHint;
	std::function<void()> onRematch;
	std::function<void()> onExit;

	DouDizhuTable(Display* display, lv_obj_t* screen);
	~DouDizhuTable();

	void render(const State& st);

	// ── 手牌选择 ──
	int  getSelectedCards(uint8_t* out, int cap) const;
	void selectCards(const uint8_t* cards, int count);   // 高亮（提示）
	void clearSelection();

	// ── 手柄导航 ──
	void moveCursor(int dir);
	void pressCurrent();       // A：切换当前光标处选中
	void setStatus(const char* text);

private:
	Display* m_display;
	lv_obj_t* m_screen;

	// ── 手牌 Sprite 池 ──
	struct Sprite {
		lv_obj_t* obj = nullptr;
		lv_obj_t* label = nullptr;
		void create(lv_obj_t* parent, int w, int h);
		void showCard(uint8_t card, bool small);
		void showBack();
		void hide();
	};
	static constexpr int POOL_PLAY = DouDizhuLogic::MAX_HAND;
	static constexpr int POOL_BACK = DouDizhuLogic::HAND_SIZE;

	Sprite m_hand[DouDizhuLogic::MAX_HAND];
	Sprite m_myPlay[POOL_PLAY];
	Sprite m_oppPlay[2][POOL_PLAY];
	Sprite m_oppBack[2][POOL_BACK];
	Sprite m_bottom[DouDizhuLogic::BOTTOM_SIZE];

	// ── 标签 ──
	lv_obj_t* m_nameLbl[3]{};
	lv_obj_t* m_scoreLbl[3]{};
	lv_obj_t* m_passLbl[2]{};       // 两个对手"不出"
	lv_obj_t* m_myPassLbl{};
	lv_obj_t* m_bidInfoLbl{};
	lv_obj_t* m_turnLbl{};
	lv_obj_t* m_statusLbl{};
	lv_obj_t* m_landlordLbl{};
	lv_obj_t* m_settleTitle{};
	lv_obj_t* m_settleDetail{};

	// ── 按钮 ──
	lv_obj_t* m_bidBtns[4]{};       // 不叫/1分/2分/3分
	lv_obj_t* m_actBtns[3]{};       // 出牌/不出/提示
	lv_obj_t* m_settleBtns[2]{};    // 再来一局/返回大厅
	lv_obj_t* m_exitBtn{};

	// ── 状态 ──
	State m_st;
	uint8_t m_sel[DouDizhuLogic::MAX_HAND]{};
	int m_cursor = 0;

	static constexpr int HAND_W = 70;
	static constexpr int HAND_H = 98;
	static constexpr int HAND_OVERLAP = 30;   // 备用（handX 用动态间距）
	static constexpr int SMALL_W = 48;
	static constexpr int SMALL_H = 70;
	static constexpr int BACK_W = 20;
	static constexpr int BACK_H = 28;

	// ── 方法 ──
	void createStatic();
	void createSprites();

	void renderAll();
	void renderHand();
	void renderOpponent(int which);       // which: 0=左(seat+1) 1=右(seat+2)
	void renderMyPlay();
	void renderBottom();
	void renderLabels();
	void renderBidButtons();
	void renderActionButtons();
	void renderSettle();

	int  seatOf(int which) const;         // 0→seat+1, 1→seat+2
	int  handX(int index) const;          // 手牌第 index 张 x 坐标

	void updateBidButtonsEnabled();
	void refreshSelectionVisual();

	// 静态回调
	static void onCardClickCb(lv_event_t* e);
	static void onBidBtnCb(lv_event_t* e);
	static void onActBtnCb(lv_event_t* e);
	static void onSettleBtnCb(lv_event_t* e);
	static void onExitCb(lv_event_t* e);
};
