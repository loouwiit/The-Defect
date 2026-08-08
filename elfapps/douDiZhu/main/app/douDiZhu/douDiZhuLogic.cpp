#include "douDiZhuLogic.hpp"

#include <cstring>
#include <cstdlib>
#include <algorithm>

// ════════════════════════════════════════════════════════════════
//  牌基础
// ════════════════════════════════════════════════════════════════

int DouDizhuLogic::cardRank(uint8_t card)
{
	if (card < 52) return card / 4;
	return card == 52 ? 13 : 14;   // 52=小王, 53=大王
}

int DouDizhuLogic::cardSuit(uint8_t card)
{
	if (card >= 52) return -1;
	return card % 4;               // 0=黑桃 1=红桃 2=梅花 3=方块
}

bool DouDizhuLogic::isRed(uint8_t card)
{
	int s = cardSuit(card);
	return s == 1 || s == 3;
}

const char* DouDizhuLogic::rankText(uint8_t card)
{
	static const char* ranks[] = {
		"3","4","5","6","7","8","9","10","J","Q","K","A","2","小王","大王"
	};
	int r = cardRank(card);
	return (r >= 0 && r <= 14) ? ranks[r] : "?";
}

const char* DouDizhuLogic::suitSymbol(uint8_t card)
{
	static const char* suits[] = { "♠", "♥", "♣", "♦" };
	int s = cardSuit(card);
	return s >= 0 ? suits[s] : "";
}

// ════════════════════════════════════════════════════════════════
//  洗牌发牌
// ════════════════════════════════════════════════════════════════

void DouDizhuLogic::shuffleDeck(uint8_t deck[DECK_SIZE])
{
	for (int i = 0; i < DECK_SIZE; i++) deck[i] = (uint8_t)i;
	// Fisher-Yates
	for (int i = DECK_SIZE - 1; i > 0; i--) {
		int j = rand() % (i + 1);
		std::swap(deck[i], deck[j]);
	}
}

void DouDizhuLogic::deal(const uint8_t deck[DECK_SIZE],
                         uint8_t hands[3][MAX_HAND], int counts[3], uint8_t bottom[BOTTOM_SIZE])
{
	int idx = 0;
	for (int s = 0; s < MAX_PLAYERS; s++) {
		counts[s] = HAND_SIZE;
		for (int k = 0; k < HAND_SIZE; k++) hands[s][k] = deck[idx++];
		sortHand(hands[s], HAND_SIZE);
	}
	for (int k = 0; k < BOTTOM_SIZE; k++) bottom[k] = deck[idx++];
}

void DouDizhuLogic::sortHand(uint8_t* cards, int count)
{
	std::sort(cards, cards + count, [](uint8_t a, uint8_t b) {
		int ra = cardRank(a), rb = cardRank(b);
		if (ra != rb) return ra > rb;         // 降序：大牌在左（大王最左）
		return a > b;
	});
}

// ════════════════════════════════════════════════════════════════
//  牌型判定
// ════════════════════════════════════════════════════════════════

const char* DouDizhuLogic::comboName(Combo::Type t)
{
	switch (t) {
	case Combo::Type::Single:       return "单张";
	case Combo::Type::Pair:         return "对子";
	case Combo::Type::Triple:       return "三张";
	case Combo::Type::TripleSingle: return "三带一";
	case Combo::Type::TriplePair:   return "三带二";
	case Combo::Type::Straight:     return "顺子";
	case Combo::Type::PairStraight: return "连对";
	case Combo::Type::Plane:        return "飞机";
	case Combo::Type::PlaneSingle:  return "飞机带单";
	case Combo::Type::PlanePair:    return "飞机带对";
	case Combo::Type::FourTwo:      return "四带二";
	case Combo::Type::FourTwoPair:  return "四带两对";
	case Combo::Type::Bomb:         return "炸弹";
	case Combo::Type::Rocket:       return "王炸";
	default:                        return "无效";
	}
}

DouDizhuLogic::Combo DouDizhuLogic::analyze(const uint8_t* cards, int count)
{
	Combo c;
	c.cardCount = count;
	if (count <= 0 || count > MAX_HAND) return c;

	// 王炸
	if (count == 2) {
		bool small = false, big = false;
		for (int i = 0; i < count; i++) {
			int r = cardRank(cards[i]);
			if (r == 13) small = true;
			else if (r == 14) big = true;
		}
		if (small && big) { c.type = Combo::Type::Rocket; c.mainRank = 14; return c; }
	}

	int rankCnt[15] = {};
	for (int i = 0; i < count; i++) rankCnt[cardRank(cards[i])]++;

	int singles = 0, pairs = 0, triples = 0, quads = 0;
	for (int r = 0; r < 15; r++) {
		if (rankCnt[r] == 1) singles++;
		else if (rankCnt[r] == 2) pairs++;
		else if (rankCnt[r] == 3) triples++;
		else if (rankCnt[r] == 4) quads++;
	}
	auto rankOf = [&](int cnt) -> int {
		for (int r = 0; r < 15; r++) if (rankCnt[r] == cnt) return r;
		return -1;
	};

	if (count == 1) { c.type = Combo::Type::Single; c.mainRank = rankOf(1); return c; }
	if (count == 2) { int r = rankOf(2); if (r >= 0) { c.type = Combo::Type::Pair; c.mainRank = r; } return c; }
	if (count == 3) { int r = rankOf(3); if (r >= 0) { c.type = Combo::Type::Triple; c.mainRank = r; } return c; }
	if (count == 4) {
		if (quads == 1) { c.type = Combo::Type::Bomb; c.mainRank = rankOf(4); }
		else if (triples == 1 && singles == 1) { c.type = Combo::Type::TripleSingle; c.mainRank = rankOf(3); }
		return c;
	}

	// ── count >= 5 ──
	// 顺子（单连 ≥5，不含 2/王）
	if (count >= 5 && singles == count) {
		int mn = 15, mx = -1;
		for (int r = 0; r < 15; r++) if (rankCnt[r] > 0) { if (r < mn) mn = r; if (r > mx) mx = r; }
		if (mx <= 11 && (mx - mn + 1) == count) {
			c.type = Combo::Type::Straight; c.mainRank = mx; c.length = count; return c;
		}
	}
	// 连对（≥3 对，不含 2/王）
	if (count >= 6 && (count & 1) == 0 && pairs == count / 2) {
		int mn = 15, mx = -1;
		for (int r = 0; r < 15; r++) if (rankCnt[r] == 2) { if (r < mn) mn = r; if (r > mx) mx = r; }
		if (mx <= 11 && (mx - mn + 1) == pairs) {
			c.type = Combo::Type::PairStraight; c.mainRank = mx; c.length = pairs; return c;
		}
	}
	// 飞机（≥2 连三，可带单/带对）
	for (int L = count / 3; L >= 2; L--) {
		for (int s = 0; s + L - 1 <= 11; s++) {
			bool ok = true;
			for (int r = s; r < s + L && ok; r++) if (rankCnt[r] < 3) ok = false;
			if (!ok) continue;
			int main = s + L - 1;
			int remain = count - 3 * L;
			if (remain == 0) { c.type = Combo::Type::Plane; c.mainRank = main; c.length = L; return c; }
			if (remain == L) {
				int wings = 0;
				for (int r = 0; r < 15; r++) {
					int use = (r >= s && r < s + L) ? 3 : 0;
					if (rankCnt[r] - use == 1) wings++;
				}
				if (wings >= L) { c.type = Combo::Type::PlaneSingle; c.mainRank = main; c.length = L; return c; }
			}
			if (remain == 2 * L) {
				int wings = 0;
				for (int r = 0; r < 15; r++) {
					int use = (r >= s && r < s + L) ? 3 : 0;
					if (rankCnt[r] - use == 2) wings++;
				}
				if (wings >= L) { c.type = Combo::Type::PlanePair; c.mainRank = main; c.length = L; return c; }
			}
		}
	}
	// 三带二（5 张）
	if (count == 5 && triples == 1 && pairs == 1) {
		c.type = Combo::Type::TriplePair; c.mainRank = rankOf(3); return c;
	}
	// 四带二（6 张：1 对 或 2 单均视为四带二）
	if (count == 6 && quads == 1) { c.type = Combo::Type::FourTwo; c.mainRank = rankOf(4); return c; }
	// 四带两对（8 张）
	if (count == 8 && quads == 1 && pairs >= 2) {
		c.type = Combo::Type::FourTwoPair; c.mainRank = rankOf(4); return c;
	}
	c.type = Combo::Type::None;
	return c;
}

bool DouDizhuLogic::canBeat(const Combo& cur, const Combo& prev)
{
	if (cur.type == Combo::Type::None) return false;
	if (prev.type == Combo::Type::None) return true;          // 先手任意
	if (cur.type == Combo::Type::Rocket) return true;         // 王炸最大
	if (prev.type == Combo::Type::Rocket) return false;
	if (cur.type == Combo::Type::Bomb) {
		if (prev.type == Combo::Type::Bomb) return cur.mainRank > prev.mainRank;
		return true;
	}
	if (prev.type == Combo::Type::Bomb) return false;
	if (cur.type != prev.type) return false;
	if (cur.length != prev.length) return false;
	return cur.mainRank > prev.mainRank;
}

bool DouDizhuLogic::isValidPlay(const uint8_t* cards, int count, const Combo& prev)
{
	if (count <= 0 || count > MAX_HAND) return false;
	Combo c = analyze(cards, count);
	return canBeat(c, prev);
}

// ════════════════════════════════════════════════════════════════
//  出法枚举 + 提示
// ════════════════════════════════════════════════════════════════

void DouDizhuLogic::enumeratePlays(const uint8_t* hand, int count, const Combo& prev,
                                   const std::function<bool(const uint8_t*, int)>& emit)
{
	if (count <= 0 || count > MAX_HAND) return;

	// 统计手牌各点实际卡
	int rankCnt[15] = {};
	uint8_t rankCards[15][4]; int rankCardCnt[15] = {};
	for (int i = 0; i < count; i++) {
		int r = cardRank(hand[i]);
		rankCnt[r]++;
		if (rankCardCnt[r] < 4) rankCards[r][rankCardCnt[r]++] = hand[i];
	}

	uint8_t sel[MAX_HAND];
	int selCnt = 0;
	auto tryCombo = [&](int n) -> bool {
		Combo c = analyze(sel, n);
		if (canBeat(c, prev)) {
			if (!emit(sel, n)) return false;
		}
		return true;
	};
	// 组装：ranks[i] 取 takes[i] 张
	auto build = [&](const int* ranks, const int* takes, int n) -> bool {
		selCnt = 0;
		for (int i = 0; i < n; i++) {
			int r = ranks[i], t = takes[i];
			for (int k = 0; k < t && k < rankCardCnt[r]; k++) sel[selCnt++] = rankCards[r][k];
		}
		return tryCombo(selCnt);
	};

	// 单张 / 对子 / 三张 / 炸弹
	for (int r = 0; r < 15; r++) {
		if (rankCnt[r] >= 1) { int rk[1] = { r }, tk[1] = { 1 }; if (!build(rk, tk, 1)) return; }
		if (rankCnt[r] >= 2) { int rk[1] = { r }, tk[1] = { 2 }; if (!build(rk, tk, 1)) return; }
		if (rankCnt[r] >= 3) { int rk[1] = { r }, tk[1] = { 3 }; if (!build(rk, tk, 1)) return; }
		if (rankCnt[r] == 4) { int rk[1] = { r }, tk[1] = { 4 }; if (!build(rk, tk, 1)) return; }
	}
	// 王炸
	if (rankCnt[13] > 0 && rankCnt[14] > 0) {
		int rk[2] = { 13, 14 }, tk[2] = { 1, 1 };
		if (!build(rk, tk, 2)) return;
	}
	// 三带一 / 三带二
	for (int r = 0; r < 15; r++) {
		if (rankCnt[r] < 3) continue;
		// 找单牌（不同点优先，同点有 4 张也行）
		for (int s = 0; s < 15; s++) {
			if (s == r && rankCnt[s] < 4) continue;
			if (rankCnt[s] - (s == r ? 3 : 0) >= 1) {
				int rk[2] = { r, s }, tk[2] = { 3, 1 };
				if (!build(rk, tk, 2)) return;
				break;
			}
		}
		for (int s = 0; s < 15; s++) {
			if (s == r) continue;
			if (rankCnt[s] >= 2) {
				int rk[2] = { r, s }, tk[2] = { 3, 2 };
				if (!build(rk, tk, 2)) return;
				break;
			}
		}
	}
	// 顺子
	for (int s = 0; s <= 7; s++) {
		for (int len = 5; s + len - 1 <= 11; len++) {
			bool ok = true;
			for (int r = s; r < s + len && ok; r++) if (rankCnt[r] < 1) ok = false;
			if (!ok) break;
			int rk[12], tk[12];
			for (int i = 0; i < len; i++) { rk[i] = s + i; tk[i] = 1; }
			if (!build(rk, tk, len)) return;
		}
	}
	// 连对
	for (int s = 0; s <= 7; s++) {
		for (int len = 3; s + len - 1 <= 11; len++) {
			bool ok = true;
			for (int r = s; r < s + len && ok; r++) if (rankCnt[r] < 2) ok = false;
			if (!ok) break;
			int rk[12], tk[12];
			for (int i = 0; i < len; i++) { rk[i] = s + i; tk[i] = 2; }
			if (!build(rk, tk, len)) return;
		}
	}
	// 飞机（裸 / 带单 / 带对）
	for (int s = 0; s <= 10; s++) {
		for (int len = 2; s + len - 1 <= 11; len++) {
			bool ok = true;
			for (int r = s; r < s + len && ok; r++) if (rankCnt[r] < 3) ok = false;
			if (!ok) break;
			int rk[12], tk[12];
			for (int i = 0; i < len; i++) { rk[i] = s + i; tk[i] = 3; }
			// 裸飞机
			if (!build(rk, tk, len)) return;
			// 带单：贪心取 len 张单牌
			int wings = 0;
			for (int r = 0; r < 15 && wings < len; r++) {
				int use = (r >= s && r < s + len) ? 3 : 0;
				if (rankCnt[r] - use >= 1) {
					rk[len + wings] = r; tk[len + wings] = 1; wings++;
				}
			}
			if (wings == len) { if (!build(rk, tk, len + wings)) return; }
			// 带对：贪心取 len 对
			wings = 0;
			for (int r = 0; r < 15 && wings < len; r++) {
				int use = (r >= s && r < s + len) ? 3 : 0;
				if (rankCnt[r] - use >= 2) {
					rk[len + wings] = r; tk[len + wings] = 2; wings++;
				}
			}
			if (wings == len) { if (!build(rk, tk, len + wings)) return; }
		}
	}
	// 四带二 / 四带两对
	for (int r = 0; r < 15; r++) {
		if (rankCnt[r] < 4) continue;
		// 四带二（两张任意，优先不同点单张）
		{
			int rk[3] = { r, -1, -1 }, tk[3] = { 4, 1, 1 };
			int wing = 0;
			for (int s = 0; s < 15 && wing < 2; s++) {
				if (s == r) continue;
				if (rankCnt[s] >= 1) { rk[1 + wing] = s; tk[1 + wing] = 1; wing++; }
			}
			if (wing == 2) { if (!build(rk, tk, 3)) return; }
		}
		// 四带两对
		{
			int rk[3] = { r, -1, -1 }, tk[3] = { 4, 2, 2 };
			int wing = 0;
			for (int s = 0; s < 15 && wing < 2; s++) {
				if (s == r) continue;
				if (rankCnt[s] >= 2) { rk[1 + wing] = s; tk[1 + wing] = 2; wing++; }
			}
			if (wing == 2) { if (!build(rk, tk, 3)) return; }
		}
	}
}

int DouDizhuLogic::findHint(const uint8_t* hand, int count, const Combo& prev,
                            uint8_t* outCards, int outCap)
{
	int bestCount = 0;
	uint8_t best[DouDizhuLogic::MAX_HAND];
	int bestMain = 999;
	int bestLen = 999;

	enumeratePlays(hand, count, prev, [&](const uint8_t* cards, int n) -> bool {
		// 优先张数少，其次主牌小，其次长度短
		int mainRank = -1, len = 0;
		Combo c = analyze(cards, n);
		mainRank = c.mainRank; len = c.length;
		if (bestCount == 0 || n < bestCount ||
			(n == bestCount && mainRank < bestMain) ||
			(n == bestCount && mainRank == bestMain && len < bestLen)) {
			bestCount = n;
			bestMain = mainRank;
			bestLen = len;
			memcpy(best, cards, n * sizeof(uint8_t));
		}
		return true;
	});

	if (bestCount == 0 || bestCount > outCap) return 0;
	memcpy(outCards, best, bestCount * sizeof(uint8_t));
	return bestCount;
}

// ════════════════════════════════════════════════════════════════
//  对局状态机
// ════════════════════════════════════════════════════════════════

void DouDizhuGame::startGame(int newDealer)
{
	memset(hands, 0, sizeof(hands));
	memset(bottom, 0, sizeof(bottom));
	memset(trickCards, 0, sizeof(trickCards));
	memset(trickCount, 0, sizeof(trickCount));
	memset(trickPass, 0, sizeof(trickPass));
	memset(passed, 0, sizeof(passed));
	memset(bidDone, 0, sizeof(bidDone));
	memset(delta, 0, sizeof(delta));

	uint8_t deck[DouDizhuLogic::DECK_SIZE];
	DouDizhuLogic::shuffleDeck(deck);
	DouDizhuLogic::deal(deck, hands, handCount, bottom);

	dealer = newDealer;
	bidTurn = dealer;
	maxBid = 0;
	maxBidder = -1;
	bidValue = 0;
	landlord = -1;
	turn = -1;
	lastPlaySeat = -1;
	lastCombo = DouDizhuLogic::Combo();
	multiplier = 1;
	farmerPlays = 0;
	landlordPlays = 0;
	landlordWin = false;
	spring = false;
	reverseSpring = false;
	phase = Phase::Bidding;
}

// ════════════════════════════════════════════════════════════════
//  叫分：每人一轮（dealer → 下家 → 再下家），可叫 1/2/3（必须高于当前）或不叫。
//  叫 3 直接当地主；若全不叫 → 重发（BidResult::Redeal）；否则最高叫分者为地主。
// ════════════════════════════════════════════════════════════════

DouDizhuGame::BidResult DouDizhuGame::applyBid(int seat, int value)
{
	if (phase != Phase::Bidding || seat != bidTurn) return BidResult::Continue;
	if (value < 0 || value > 3) return BidResult::Continue;

	if (value == 0) {
		passed[seat] = true;
	} else {
		if (value <= maxBid) return BidResult::Continue;   // 必须高于当前
		maxBid = value;
		maxBidder = seat;
		if (value == 3) {   // 叫 3 直接当地主
			landlord = seat;
			bidValue = 3;
			enterPlaying();
			return BidResult::Landlord;
		}
	}
	bidDone[seat] = true;

	// 找下一个未行动者
	int next = -1;
	for (int i = 1; i <= 3; i++) {
		int s = (seat + i) % 3;
		if (!bidDone[s]) { next = s; break; }
	}
	if (next < 0) {   // 所有人都行动过
		if (maxBid == 0) {   // 全不叫 → 重发
			phase = Phase::Waiting;
			return BidResult::Redeal;
		}
		landlord = maxBidder;
		bidValue = maxBid;
		enterPlaying();
		return BidResult::Landlord;
	}
	bidTurn = next;
	return BidResult::Continue;
}

void DouDizhuGame::enterPlaying()
{
	// 地主拿 3 张底牌 → 手牌变为 20 张
	for (int i = 0; i < DouDizhuLogic::BOTTOM_SIZE; i++) {
		hands[landlord][handCount[landlord]++] = bottom[i];
	}
	DouDizhuLogic::sortHand(hands[landlord], handCount[landlord]);

	phase = Phase::Playing;
	bidTurn = -1;
	turn = landlord;
	lastPlaySeat = -1;   // 地主先出（必须先出）
	lastCombo = DouDizhuLogic::Combo();
}

bool DouDizhuGame::applyPlay(int seat, const uint8_t* cards, int count)
{
	if (phase != Phase::Playing || seat != turn) return false;
	if (count <= 0 || count > DouDizhuLogic::MAX_HAND) return false;

	// 校验
	DouDizhuLogic::Combo c = DouDizhuLogic::analyze(cards, count);
	if (c.type == DouDizhuLogic::Combo::Type::None) return false;
	// 新墩（先手）可出任意牌：mustLead 时 prev 重置为 None，无需压过上一手
	DouDizhuLogic::Combo prev = lastCombo;
	if (mustLead(seat)) prev = DouDizhuLogic::Combo{};
	if (!DouDizhuLogic::canBeat(c, prev)) return false;

	// 校验手牌确实包含这些牌
	for (int i = 0; i < count; i++) {
		if (!hasCard(seat, cards[i])) return false;
	}
	if (!removeCards(seat, cards, count)) return false;

	// 更新局面
	lastCombo = c;
	lastPlaySeat = seat;
	memcpy(trickCards[seat], cards, count * sizeof(uint8_t));
	trickCount[seat] = count;
	trickPass[seat] = false;

	if (c.type == DouDizhuLogic::Combo::Type::Bomb ||
		c.type == DouDizhuLogic::Combo::Type::Rocket) {
		multiplier *= 2;
	}
	if (seat == landlord) landlordPlays++;
	else farmerPlays++;

	if (handCount[seat] == 0) {
		finish();
		return true;
	}

	turn = nextActive(seat);
	return true;
}

void DouDizhuGame::applyPass(int seat)
{
	if (phase != Phase::Playing || seat != turn) return;
	if (mustLead(seat)) return;        // 先手不能不出

	trickPass[seat] = true;
	trickCount[seat] = 0;
	turn = nextActive(seat);
}

int DouDizhuGame::nextActive(int seat) const
{
	// 3 人局无出局，直接下一家
	return seatNext(seat);
}

bool DouDizhuGame::mustLead(int seat) const
{
	if (phase != Phase::Playing || seat != turn) return false;
	if (lastPlaySeat == -1) return true;      // 首出
	return seat == lastPlaySeat;              // 墩回到自己（另两家都过）
}

void DouDizhuGame::finish()
{
	if (phase != Phase::Playing) return;
	landlordWin = (handCount[landlord] == 0);
	spring = landlordWin && farmerPlays == 0;
	reverseSpring = !landlordWin && landlordPlays == 1;
	if (spring || reverseSpring) multiplier *= 2;

	int base = bidValue > 0 ? bidValue : 1;
	int mult = multiplier;
	if (landlordWin) {
		delta[landlord] = 2 * base * mult;
		for (int i = 0; i < 3; i++) if (i != landlord) delta[i] = -base * mult;
	} else {
		delta[landlord] = -2 * base * mult;
		for (int i = 0; i < 3; i++) if (i != landlord) delta[i] = base * mult;
	}
	for (int i = 0; i < 3; i++) scores[i] += delta[i];
	phase = Phase::Settle;
}

bool DouDizhuGame::hasCard(int seat, uint8_t card) const
{
	for (int i = 0; i < handCount[seat]; i++)
		if (hands[seat][i] == card) return true;
	return false;
}

bool DouDizhuGame::removeCards(int seat, const uint8_t* cards, int count)
{
	uint8_t* hand = hands[seat];
	int& n = handCount[seat];
	for (int i = 0; i < count; i++) {
		int idx = -1;
		for (int j = 0; j < n; j++) if (hand[j] == cards[i]) { idx = j; break; }
		if (idx < 0) return false;
		hand[idx] = hand[n - 1];
		n--;
	}
	DouDizhuLogic::sortHand(hand, n);
	return true;
}
