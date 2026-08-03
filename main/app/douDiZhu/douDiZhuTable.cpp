#include "douDiZhuTable.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"
#include "app/app.hpp"

#include <cstring>
#include <cstdio>
#include <esp_log.h>

static constexpr char TAG[] = "DouDizhuTable";

static constexpr uint32_t C_BG       = 0x0D0D1A;
static constexpr uint32_t C_CARD_FACE = 0xFFFFFF;
static constexpr uint32_t C_CARD_BACK = 0x1E5AA8;
static constexpr uint32_t C_RED      = 0xE53935;
static constexpr uint32_t C_BLACK    = 0x1A1A2E;
static constexpr uint32_t C_SUBTLE   = 0x888888;
static constexpr uint32_t C_ACCENT   = 0x4A6CF7;
static constexpr uint32_t C_TURN     = 0xFFC107;
static constexpr uint32_t C_BTN      = 0x2D2D3D;
static constexpr uint32_t C_DISABLED = 0x3A3A4A;

// ════════════════════════════════════════════════════════════════
//  Sprite（单张扑克牌）
// ════════════════════════════════════════════════════════════════

void DouDizhuTable::Sprite::create(lv_obj_t* parent, int w, int h)
{
	obj = lv_obj_create(parent);
	lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(obj, w, h);
	lv_obj_set_style_radius(obj, 6, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_color(obj, lv_color_hex(0x999999), 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_60, 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(C_CARD_FACE), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_shadow_width(obj, 4, 0);
	lv_obj_set_style_shadow_color(obj, lv_color_hex(0x000000), 0);
	lv_obj_set_style_shadow_opa(obj, LV_OPA_40, 0);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

	label = lv_label_create(obj);
	lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_pos(label, 3, 2);   // 左上角对齐，点数不被遮挡
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
	lv_obj_set_style_text_line_space(label, -12, 0);   // 竖排 JOKER 紧凑
	lv_obj_set_width(label, lv_pct(100));
	lv_obj_set_style_pad_all(label, 0, 0);
	lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
}

void DouDizhuTable::Sprite::showCard(uint8_t card, bool small)
{
	if (!obj) return;
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_color(obj, lv_color_hex(C_CARD_FACE), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

	int r = DouDizhuLogic::cardRank(card);
	bool isJoker = (r >= 13);
	char buf[12];
	if (isJoker) {
		// 大小王竖列显示 JOKER（大王红/小王黑），出牌区小卡用"王"
		snprintf(buf, sizeof(buf), "%s", small ? "王" : "J\nO\nK\nE\nR");
	} else if (small) {
		// 出牌区小卡：点数与花色竖排
		snprintf(buf, sizeof(buf), "%s\n%s", DouDizhuLogic::rankText(card), DouDizhuLogic::suitSymbol(card));
	} else {
		snprintf(buf, sizeof(buf), "%s%s", DouDizhuLogic::rankText(card), DouDizhuLogic::suitSymbol(card));
	}
	lv_label_set_text(label, buf);
	// 字体：JOKER 与出牌区小卡用 Small，手牌普通牌用 Medium
	const lv_font_t* font = FontLoader::getDefault(FontLoader::FontSize::Small);
	if (!small && !isJoker) font = FontLoader::getDefault(FontLoader::FontSize::Medium);
	lv_obj_set_style_text_font(label, font, 0);

	bool red = DouDizhuLogic::isRed(card) || r == 14;   // 红桃/方块/大王 红色
	lv_obj_set_style_text_color(label, lv_color_hex(red ? C_RED : C_BLACK), 0);
}

void DouDizhuTable::Sprite::showBack()
{
	if (!obj) return;
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_color(obj, lv_color_hex(C_CARD_BACK), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_label_set_text(label, "");
}

void DouDizhuTable::Sprite::hide()
{
	if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

DouDizhuTable::DouDizhuTable(Display* display, lv_obj_t* screen)
	: m_display(display), m_screen(screen)
{
	createStatic();
	createSprites();
}

DouDizhuTable::~DouDizhuTable() = default;

// ════════════════════════════════════════════════════════════════
//  静态 UI（标签 + 按钮）
// ════════════════════════════════════════════════════════════════

void DouDizhuTable::createStatic()
{
	auto makeLabel = [&](lv_obj_t*& lbl, lv_coord_t x, lv_coord_t y, int w,
	                      const char* text, uint32_t color, bool center,
	                      const lv_font_t* font = FontLoader::getDefault(FontLoader::FontSize::Small)) {
		lbl = lv_label_create(m_screen);
		lv_label_set_text(lbl, text);
		lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
		lv_obj_set_style_text_font(lbl, font, 0);
		lv_obj_set_pos(lbl, x, y);
		if (w > 0) {
			lv_obj_set_width(lbl, w);
			lv_obj_set_style_text_align(lbl, center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
		}
	};

	// 三个玩家名称（对手顶部左右 + 本机底部，Medium 字体）
	auto fntMed = FontLoader::getDefault(FontLoader::FontSize::Medium);
	makeLabel(m_nameLbl[1], 110, 10, 220, "玩家", C_SUBTLE, false, fntMed);
	makeLabel(m_nameLbl[2], 1280 - 330, 10, 220, "玩家", C_SUBTLE, true, fntMed);
	makeLabel(m_nameLbl[0], 20, 662, 130, "我", C_ACCENT, false, fntMed);

	makeLabel(m_scoreLbl[1], 110, 48, 220, "", C_SUBTLE, false, fntMed);
	makeLabel(m_scoreLbl[2], 1280 - 330, 48, 220, "", C_SUBTLE, true, fntMed);
	makeLabel(m_scoreLbl[0], 110, 668, 340, "", C_SUBTLE, false);

	// 两个对手"不出"标记
	makeLabel(m_passLbl[0], 150, 150, 120, "不出", C_TURN, true, fntMed);
	makeLabel(m_passLbl[1], 1280 - 270, 150, 120, "不出", C_TURN, true, fntMed);
	lv_obj_add_flag(m_passLbl[0], LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_passLbl[1], LV_OBJ_FLAG_HIDDEN);

	// 我方"不出"
	makeLabel(m_myPassLbl, 640 - 120, 425, 240, "不出", C_TURN, true, fntMed);
	lv_obj_add_flag(m_myPassLbl, LV_OBJ_FLAG_HIDDEN);

	// 顶部中央（底牌下方，避开左右对手区）：底分/地主/回合/状态
	makeLabel(m_bidInfoLbl, 460, 120, 360, "", C_SUBTLE, true, fntMed);
	makeLabel(m_landlordLbl, 460, 154, 360, "", C_SUBTLE, true, fntMed);
	makeLabel(m_turnLbl, 460, 190, 360, "", C_TURN, true, fntMed);
	makeLabel(m_statusLbl, 460, 226, 360, "", C_SUBTLE, true);

	// 结算（title/detail/按钮垂直错开，避免遮挡）
	makeLabel(m_settleTitle, 320, 200, 640, "", C_TURN, true);
	lv_obj_set_style_text_font(m_settleTitle, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	makeLabel(m_settleDetail, 320, 280, 640, "", C_SUBTLE, true, fntMed);
	lv_obj_add_flag(m_settleTitle, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_settleDetail, LV_OBJ_FLAG_HIDDEN);

	// ── 退出按钮 ──
	m_exitBtn = GUI::createButton(m_screen, "← 退出", 100, 44);
	lv_obj_set_pos(m_exitBtn, 8, 8);
	lv_obj_set_style_bg_color(m_exitBtn, lv_color_hex(C_BTN), 0);
	lv_obj_set_style_border_width(m_exitBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_exitBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_exitBtn, onExitCb, LV_EVENT_CLICKED, this);

	// ── 叫分按钮（不叫/1分/2分/3分） ──
	const char* bidTexts[4] = { "不叫", "1分", "2分", "3分" };
	for (int i = 0; i < 4; i++) {
		m_bidBtns[i] = GUI::createButton(m_screen, bidTexts[i], 120, 54);
		lv_obj_set_style_bg_color(m_bidBtns[i], lv_color_hex(C_BTN), 0);
		lv_obj_set_style_border_width(m_bidBtns[i], 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(m_bidBtns[i], lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_add_event_cb(m_bidBtns[i], onBidBtnCb, LV_EVENT_CLICKED, this);
		lv_obj_add_flag(m_bidBtns[i], LV_OBJ_FLAG_HIDDEN);
	}
	// 位置：居中一行 y=495（手牌上方）
	for (int i = 0; i < 4; i++)
		lv_obj_set_pos(m_bidBtns[i], 640 - 2 * 120 - 3 * 12 + i * (120 + 12), 495);

	// ── 出牌/不出/提示按钮 —— 手牌上方居中（仅轮到本机时显示） ──
	const char* actTexts[3] = { "出牌", "不出", "提示" };
	for (int i = 0; i < 3; i++) {
		m_actBtns[i] = GUI::createButton(m_screen, actTexts[i], 130, 54);
		lv_obj_set_pos(m_actBtns[i], 640 - (3 * 130 + 2 * 12) / 2 + i * (130 + 12), 495);
		lv_obj_set_style_bg_color(m_actBtns[i], lv_color_hex(C_BTN), 0);
		lv_obj_set_style_border_width(m_actBtns[i], 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(m_actBtns[i], lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_add_event_cb(m_actBtns[i], onActBtnCb, LV_EVENT_CLICKED, this);
		lv_obj_add_flag(m_actBtns[i], LV_OBJ_FLAG_HIDDEN);
	}

	// ── 结算按钮（再来一局/返回大厅） ──
	const char* settleTexts[2] = { "再来一局", "返回大厅" };
	for (int i = 0; i < 2; i++) {
		m_settleBtns[i] = GUI::createButton(m_screen, settleTexts[i], 220, 60);
		lv_obj_set_pos(m_settleBtns[i], 640 - 220 - 8 + i * 228, 400);
		lv_obj_set_style_bg_color(m_settleBtns[i], lv_color_hex(C_BTN), 0);
		lv_obj_set_style_border_width(m_settleBtns[i], 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(m_settleBtns[i], lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_add_event_cb(m_settleBtns[i], onSettleBtnCb, LV_EVENT_CLICKED, this);
		lv_obj_add_flag(m_settleBtns[i], LV_OBJ_FLAG_HIDDEN);
	}
}

void DouDizhuTable::createSprites()
{
	for (int i = 0; i < DouDizhuLogic::MAX_HAND; i++) {
		m_hand[i].create(m_screen, HAND_W, HAND_H);
		// 手牌可点击选中
		lv_obj_add_flag(m_hand[i].obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_user_data(m_hand[i].obj, reinterpret_cast<void*>((intptr_t)i));
		lv_obj_add_event_cb(m_hand[i].obj, onCardClickCb, LV_EVENT_CLICKED, this);
	}
	for (int i = 0; i < POOL_PLAY; i++) {
		m_myPlay[i].create(m_screen, SMALL_W, SMALL_H);
		for (int o = 0; o < 2; o++) m_oppPlay[o][i].create(m_screen, SMALL_W, SMALL_H);
	}
	for (int o = 0; o < 2; o++)
		for (int i = 0; i < POOL_BACK; i++) m_oppBack[o][i].create(m_screen, BACK_W, BACK_H);
	for (int i = 0; i < DouDizhuLogic::BOTTOM_SIZE; i++) m_bottom[i].create(m_screen, HAND_W, HAND_H);
}

// ════════════════════════════════════════════════════════════════
//  渲染入口
// ════════════════════════════════════════════════════════════════

int DouDizhuTable::seatOf(int which) const
{
	return (m_st.mySeat + 1 + which) % 3;
}

int DouDizhuTable::handX(int index) const
{
	int n = m_st.handCount > 0 ? m_st.handCount : 1;
	// 动态间距：每张至少露出 34px（点数清晰），总宽不超过 820px
	int step = (n > 1) ? ((920 - HAND_W) / (n - 1)) : 0;
	if (step < 34) step = 34;
	if (step > 48) step = 48;
	int total = HAND_W + (n - 1) * step;
	int start = 640 - total / 2;
	return start + index * step;
}

void DouDizhuTable::render(const State& st)
{
	m_st = st;
	if (m_cursor >= m_st.handCount) m_cursor = m_st.handCount > 0 ? m_st.handCount - 1 : 0;
	renderAll();
}

void DouDizhuTable::renderAll()
{
	renderLabels();
	renderHand();
	renderOpponent(0);
	renderOpponent(1);
	renderMyPlay();
	renderBottom();
	renderBidButtons();
	renderActionButtons();
	renderSettle();
}

// ════════════════════════════════════════════════════════════════
//  各区域渲染
// ════════════════════════════════════════════════════════════════

void DouDizhuTable::renderHand()
{
	int n = m_st.handCount;
	for (int i = 0; i < DouDizhuLogic::MAX_HAND; i++) {
		if (i < n) {
			m_hand[i].showCard(m_st.hand[i], false);
			lv_obj_set_pos(m_hand[i].obj, handX(i), 555 - (m_sel[i] ? 20 : 0));
			// 光标高亮边框
			if (i == m_cursor && m_st.phase == 2 && m_st.turn == m_st.mySeat) {
				lv_obj_set_style_border_width(m_hand[i].obj, 3, 0);
				lv_obj_set_style_border_color(m_hand[i].obj, lv_color_hex(C_TURN), 0);
			} else {
				lv_obj_set_style_border_width(m_hand[i].obj, 1, 0);
				lv_obj_set_style_border_color(m_hand[i].obj, lv_color_hex(0x999999), 0);
			}
		} else {
			m_hand[i].hide();
		}
	}
}

void DouDizhuTable::renderOpponent(int which)
{
	int seat = seatOf(which);
	int count = m_st.cardCount[seat];
	int n = count > DouDizhuLogic::MAX_HAND ? DouDizhuLogic::MAX_HAND : count;
	int backX = which == 0 ? 120 : 1280 - 120 - (BACK_W + (n - 1) * 14);
	int backY = which == 0 ? 90 : 90;
	for (int i = 0; i < POOL_BACK; i++) {
		if (i < n) {
			m_oppBack[which][i].showBack();
			lv_obj_set_pos(m_oppBack[which][i].obj, backX + i * 14, backY);
		} else {
			m_oppBack[which][i].hide();
		}
	}

	// 出牌区（小卡间距 26px）
	int pc = m_st.trickCount[seat];
	int pn = pc > POOL_PLAY ? POOL_PLAY : pc;
	int pw = SMALL_W + (pn > 0 ? (pn - 1) * 26 : 0);
	int px = which == 0 ? 120 + 170 - pw / 2 : 1280 - 120 - 170 - pw / 2;
	int py = 200;
	for (int i = 0; i < POOL_PLAY; i++) {
		if (i < pn) {
			m_oppPlay[which][i].showCard(m_st.trick[seat][i], true);
			lv_obj_set_pos(m_oppPlay[which][i].obj, px + i * 26, py);
		} else {
			m_oppPlay[which][i].hide();
		}
	}

	// "不出"标记
	if (m_st.trickPass[seat] && pn == 0) {
		lv_obj_clear_flag(m_passLbl[which], LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_pos(m_passLbl[which], which == 0 ? 120 + 170 - 60 : 1280 - 120 - 170 - 60, 200);
	} else {
		lv_obj_add_flag(m_passLbl[which], LV_OBJ_FLAG_HIDDEN);
	}
}

void DouDizhuTable::renderMyPlay()
{
	int seat = m_st.mySeat;
	int pc = m_st.trickCount[seat];
	int pn = pc > POOL_PLAY ? POOL_PLAY : pc;
	int pw = SMALL_W + (pn > 0 ? (pn - 1) * 26 : 0);
	int px = 640 - pw / 2;
	for (int i = 0; i < POOL_PLAY; i++) {
		if (i < pn) {
			m_myPlay[i].showCard(m_st.trick[seat][i], true);
			lv_obj_set_pos(m_myPlay[i].obj, px + i * 26, 430);
		} else {
			m_myPlay[i].hide();
		}
	}
	if (m_st.trickPass[seat] && pn == 0) {
		lv_obj_clear_flag(m_myPassLbl, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(m_myPassLbl, LV_OBJ_FLAG_HIDDEN);
	}
}

void DouDizhuTable::renderBottom()
{
	if (m_st.bottomRevealed) {
		int total = 3 * HAND_W + 2 * 10;
		int x = 640 - total / 2;
		for (int i = 0; i < DouDizhuLogic::BOTTOM_SIZE; i++) {
			m_bottom[i].showCard(m_st.bottom[i], false);
			lv_obj_set_pos(m_bottom[i].obj, x + i * (HAND_W + 10), 14);
		}
	} else {
		for (int i = 0; i < DouDizhuLogic::BOTTOM_SIZE; i++) m_bottom[i].hide();
	}
}

void DouDizhuTable::renderLabels()
{
	// 玩家名（地主名字加 ★ 前缀 + 醒目黄色，所有人一眼可辨）
	const char* defaultNames[3] = { "我", "玩家2", "玩家3" };
	for (int s = 0; s < 3; s++) {
		const char* name = m_st.names[s][0] ? m_st.names[s] : defaultNames[s];
		bool isLandlord = (m_st.phase == 2 || m_st.phase == 3) && m_st.landlord == s;
		char buf[80];
		if (isLandlord) snprintf(buf, sizeof(buf), "★ %s", name);
		else snprintf(buf, sizeof(buf), "%s", name);
		lv_label_set_text(m_nameLbl[s], buf);
		char score[48];
		snprintf(score, sizeof(score), "剩余%d  分:%d", m_st.cardCount[s], m_st.scores[s]);
		lv_label_set_text(m_scoreLbl[s], score);
		// 轮到谁 → 高亮名称；地主恒为醒目黄
		bool active = (m_st.phase == 2 && m_st.turn == s) ||
		              (m_st.phase == 1 && m_st.bidTurn == s);
		uint32_t col = active ? C_TURN : (s == m_st.mySeat ? C_ACCENT : C_SUBTLE);
		if (isLandlord) col = C_TURN;
		lv_obj_set_style_text_color(m_nameLbl[s], lv_color_hex(col), 0);
	}

	// 叫分信息（单行，避免括号换行）
	if (m_st.phase == 1) {
		char buf[64];
		if (m_st.maxBid > 0) {
			snprintf(buf, sizeof(buf), "最高叫分：%d分  玩家%d", m_st.maxBid, m_st.maxBidder + 1);
		} else {
			snprintf(buf, sizeof(buf), "开始叫分  先手：玩家%d", m_st.dealer + 1);
		}
		lv_label_set_text(m_bidInfoLbl, buf);
	} else if (m_st.phase == 2 || m_st.phase == 3) {
		char buf[64];
		snprintf(buf, sizeof(buf), "底分：%d  倍数：x%d", m_st.bidValue, m_st.multiplier);
		lv_label_set_text(m_bidInfoLbl, buf);
	}

	// 地主提示（醒目）
	if (m_st.landlord >= 0 && (m_st.phase == 2 || m_st.phase == 3)) {
		if (m_st.landlord == m_st.mySeat) {
			lv_label_set_text(m_landlordLbl, "★ 你是地主 ★");
			lv_obj_set_style_text_color(m_landlordLbl, lv_color_hex(C_TURN), 0);
		} else {
			char buf[32];
			snprintf(buf, sizeof(buf), "地主：玩家%d", m_st.landlord + 1);
			lv_label_set_text(m_landlordLbl, buf);
			lv_obj_set_style_text_color(m_landlordLbl, lv_color_hex(C_SUBTLE), 0);
		}
	} else {
		lv_label_set_text(m_landlordLbl, "");
	}

	// 回合指示（含倒计时）
	if (m_st.phase == 2) {
		char buf[48];
		int cd = m_st.countdownSec > 0 ? m_st.countdownSec : 0;
		if (m_st.turn == m_st.mySeat)
			snprintf(buf, sizeof(buf), "轮到您出牌  剩 %d 秒", cd);
		else
			snprintf(buf, sizeof(buf), "轮到玩家%d  剩 %d 秒", m_st.turn + 1, cd);
		lv_label_set_text(m_turnLbl, buf);
	} else {
		lv_label_set_text(m_turnLbl, "");
	}

	// 状态
	const char* status = "";
	if (!m_st.connected) status = "连接已断开";
	else if (m_st.phase == 0) status = "等待开局...";
	else if (m_st.phase == 1) {
		status = (m_st.bidTurn == m_st.mySeat) ? "轮到您叫分" : "等待其他玩家叫分...";
	} else if (m_st.phase == 2) {
		if (m_st.turn == m_st.mySeat) {
			status = m_st.myMustLead ? "轮到您出牌（必须出）" : "轮到您出牌";
		} else {
			status = "等待出牌...";
		}
	} else if (m_st.phase == 3) status = "";
	lv_label_set_text(m_statusLbl, status);
}

void DouDizhuTable::renderBidButtons()
{
	bool show = (m_st.phase == 1) && (m_st.bidTurn == m_st.mySeat);
	for (int i = 0; i < 4; i++) {
		if (show) {
			lv_obj_clear_flag(m_bidBtns[i], LV_OBJ_FLAG_HIDDEN);
			// i=0 不叫始终可用；i>=1 需 >= minBid
			bool enabled = (i == 0) || (i >= m_st.minBid);
			lv_obj_set_style_bg_color(m_bidBtns[i], lv_color_hex(enabled ? C_BTN : C_DISABLED), 0);
			lv_obj_set_style_opa(m_bidBtns[i], enabled ? LV_OPA_COVER : LV_OPA_40, 0);
		} else {
			lv_obj_add_flag(m_bidBtns[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
}

void DouDizhuTable::renderActionButtons()
{
	bool show = (m_st.phase == 2) && (m_st.turn == m_st.mySeat);
	for (int i = 0; i < 3; i++) {
		if (show) {
			lv_obj_clear_flag(m_actBtns[i], LV_OBJ_FLAG_HIDDEN);
			// 不出按钮仅在可不出时启用
			bool enabled = (i == 1) ? m_st.myCanPass : true;
			lv_obj_set_style_bg_color(m_actBtns[i], lv_color_hex(enabled ? C_BTN : C_DISABLED), 0);
			lv_obj_set_style_opa(m_actBtns[i], enabled ? LV_OPA_COVER : LV_OPA_40, 0);
		} else {
			lv_obj_add_flag(m_actBtns[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
}

void DouDizhuTable::renderSettle()
{
	bool show = (m_st.phase == 3);
	if (show) {
		lv_obj_clear_flag(m_settleTitle, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(m_settleDetail, LV_OBJ_FLAG_HIDDEN);
		for (int i = 0; i < 2; i++) lv_obj_clear_flag(m_settleBtns[i], LV_OBJ_FLAG_HIDDEN);

		bool iWin = (m_st.landlordWin && m_st.mySeat == m_st.landlord) ||
		            (!m_st.landlordWin && m_st.mySeat != m_st.landlord);
		const char* title = iWin ? "胜利！" : "失败";
		lv_label_set_text(m_settleTitle, title);
		lv_obj_set_style_text_color(m_settleTitle, lv_color_hex(iWin ? 0x44FF44 : 0xFF5252), 0);

		char detail[128];
		const char* springTxt = m_st.spring ? "  春天！" : (m_st.reverseSpring ? "  反春！" : "");
		snprintf(detail, sizeof(detail), "倍数 x%d%s\n本局：我 %+d  |  本局得分已累计", m_st.multiplier, springTxt, m_st.delta[m_st.mySeat]);
		lv_label_set_text(m_settleDetail, detail);
	} else {
		lv_obj_add_flag(m_settleTitle, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_settleDetail, LV_OBJ_FLAG_HIDDEN);
		for (int i = 0; i < 2; i++) lv_obj_add_flag(m_settleBtns[i], LV_OBJ_FLAG_HIDDEN);
	}
}

// ════════════════════════════════════════════════════════════════
//  选择 / 光标
// ════════════════════════════════════════════════════════════════

int DouDizhuTable::getSelectedCards(uint8_t* out, int cap) const
{
	int n = 0;
	for (int i = 0; i < m_st.handCount && n < cap; i++)
		if (m_sel[i]) out[n++] = m_st.hand[i];
	return n;
}

void DouDizhuTable::selectCards(const uint8_t* cards, int count)
{
	if (auto g = m_display->lockGuard()) {
		memset(m_sel, 0, sizeof(m_sel));
		for (int i = 0; i < m_st.handCount; i++) {
			for (int j = 0; j < count; j++) {
				if (m_st.hand[i] == cards[j]) { m_sel[i] = true; break; }
			}
		}
		renderHand();
	}
}

void DouDizhuTable::clearSelection()
{
	if (auto g = m_display->lockGuard()) {
		memset(m_sel, 0, sizeof(m_sel));
		renderHand();
	}
}

void DouDizhuTable::moveCursor(int dir)
{
	if (auto g = m_display->lockGuard()) {
		if (m_st.handCount <= 0) return;
		m_cursor += dir;
		if (m_cursor < 0) m_cursor = 0;
		if (m_cursor >= m_st.handCount) m_cursor = m_st.handCount - 1;
		renderHand();
	}
}

void DouDizhuTable::pressCurrent()
{
	if (auto g = m_display->lockGuard()) {
		if (m_cursor < 0 || m_cursor >= m_st.handCount) return;
		m_sel[m_cursor] = !m_sel[m_cursor];
		renderHand();
	}
}

void DouDizhuTable::setStatus(const char* text)
{
	if (auto g = m_display->lockGuard()) {
		lv_label_set_text(m_statusLbl, text);
	}
}

// ════════════════════════════════════════════════════════════════
//  静态回调
// ════════════════════════════════════════════════════════════════

void DouDizhuTable::onCardClickCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuTable*>(lv_event_get_user_data(e));
	if (!self) return;
	auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
	int idx = (int)(intptr_t)lv_obj_get_user_data(target);
	if (idx < 0 || idx >= self->m_st.handCount) return;
	// 仅轮到本机时可操作
	if (self->m_st.phase != 2 || self->m_st.turn != self->m_st.mySeat) return;
	self->m_sel[idx] = !self->m_sel[idx];
	self->renderHand();
}

void DouDizhuTable::onBidBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuTable*>(lv_event_get_user_data(e));
	if (!self) return;
	int value = -1;
	for (int i = 0; i < 4; i++) {
		if (lv_event_get_current_target(e) == self->m_bidBtns[i]) { value = i; break; }  // 0=不叫,1..3
	}
	if (value < 0) return;
	if (value > 0 && value < self->m_st.minBid) return;
	if (self->onBid) self->onBid(value);
}

void DouDizhuTable::onActBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuTable*>(lv_event_get_user_data(e));
	if (!self) return;
	if (lv_event_get_current_target(e) == self->m_actBtns[0] && self->onPlay) self->onPlay();
	else if (lv_event_get_current_target(e) == self->m_actBtns[1] && self->onPass) self->onPass();
	else if (lv_event_get_current_target(e) == self->m_actBtns[2] && self->onHint) self->onHint();
}

void DouDizhuTable::onSettleBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuTable*>(lv_event_get_user_data(e));
	if (!self) return;
	if (lv_event_get_current_target(e) == self->m_settleBtns[0] && self->onRematch) self->onRematch();
	else if (lv_event_get_current_target(e) == self->m_settleBtns[1] && self->onExit) self->onExit();
}

void DouDizhuTable::onExitCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuTable*>(lv_event_get_user_data(e));
	if (!self) return;
	if (self->onExit) self->onExit();
}
