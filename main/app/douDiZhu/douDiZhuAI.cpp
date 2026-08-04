#include "douDiZhuAI.hpp"

#include <cstring>

// ════════════════════════════════════════════════════════════════
//  叫分
// ════════════════════════════════════════════════════════════════

int DouDizhuAI::chooseBid(const uint8_t* hand, int count, int minBid, bool mustBid)
{
	if (mustBid) return minBid > 0 ? minBid : 1;

	int strength = 0;
	int rankCnt[15] = {};
	for (int i = 0; i < count; i++) rankCnt[DouDizhuLogic::cardRank(hand[i])]++;

	for (int r = 0; r < 15; r++) {
		if (rankCnt[r] == 0) continue;
		int w = 0;
		if (r <= 7)        w = 0;   // 3..10
		else if (r <= 10)  w = 1;   // J Q K
		else if (r == 11)  w = 2;   // A
		else if (r == 12)  w = 4;   // 2
		else if (r == 13)  w = 6;   // 小王
		else if (r == 14)  w = 8;   // 大王
		strength += rankCnt[r] * w;
		if (rankCnt[r] == 4) strength += 3;   // 炸弹加分
	}

	int want;
	if (strength >= 16)      want = 3;
	else if (strength >= 11) want = 2;
	else if (strength >= 7)  want = 1;
	else                     want = 0;

	if (want == 0 || want < minBid) return 0;   // 不够格则不叫
	if (want > 3) want = 3;
	return want;
}

// ════════════════════════════════════════════════════════════════
//  出牌
// ════════════════════════════════════════════════════════════════

static bool isLongCombo(DouDizhuLogic::Combo::Type t)
{
	return t == DouDizhuLogic::Combo::Type::Straight ||
	       t == DouDizhuLogic::Combo::Type::PairStraight ||
	       t == DouDizhuLogic::Combo::Type::Plane ||
	       t == DouDizhuLogic::Combo::Type::PlaneSingle ||
	       t == DouDizhuLogic::Combo::Type::PlanePair;
}

int DouDizhuAI::choosePlay(const uint8_t* hand, int count,
                           const DouDizhuLogic::Combo& prev, int prevSeat,
                           bool mustLead, int mySeat, int landlord,
                           uint8_t* outCards, int outCap)
{
	if (count <= 0 || count > DouDizhuLogic::MAX_HAND) return 0;

	// 一手出完直接赢
	{
		DouDizhuLogic::Combo c = DouDizhuLogic::analyze(hand, count);
		if (c.type != DouDizhuLogic::Combo::Type::None &&
			DouDizhuLogic::canBeat(c, prev)) {
			memcpy(outCards, hand, count * sizeof(uint8_t));
			return count;
		}
	}

	// 农民不压队友（队友出的牌放过）
	bool isFarmer = (mySeat != landlord);
	bool prevIsTeammate = isFarmer && (prevSeat != landlord) && (prevSeat != mySeat);
	if (prevIsTeammate && !mustLead && prev.type != DouDizhuLogic::Combo::Type::None) {
		return 0;
	}

	int bestCount = 0;
	uint8_t best[DouDizhuLogic::MAX_HAND];
	int bestMain = 999, bestLen = 999;
	bool bestIsBomb = false;

	DouDizhuLogic::enumeratePlays(hand, count, prev,
		[&](const uint8_t* cards, int n) -> bool {
			DouDizhuLogic::Combo c = DouDizhuLogic::analyze(cards, n);
			bool isBomb = (c.type == DouDizhuLogic::Combo::Type::Bomb ||
			               c.type == DouDizhuLogic::Combo::Type::Rocket);

			bool better = false;
			if (bestCount == 0) {
				better = true;
			} else if (mustLead) {
				// 先手：倾向出长组合清手牌，非炸弹优先，主牌小优先
				int a = n + (isLongCombo(c.type) ? 4 : 0) + (isBomb ? -10 : 0);
				int b = bestCount + (bestIsBomb ? -10 : 0);
				if (a != b) {
					better = a > b;
				} else if (c.mainRank != bestMain) {
					better = c.mainRank < bestMain;
				} else {
					better = c.length < bestLen;
				}
			} else {
				// 压牌：张数少优先，非炸弹优先，主牌小优先
				if (n != bestCount) {
					better = n < bestCount;
				} else if (isBomb != bestIsBomb) {
					better = !isBomb;
				} else if (c.mainRank != bestMain) {
					better = c.mainRank < bestMain;
				} else if (c.length != bestLen) {
					better = c.length < bestLen;
				}
			}

			if (better) {
				bestCount = n;
				bestMain = c.mainRank;
				bestLen = c.length;
				bestIsBomb = isBomb;
				memcpy(best, cards, n * sizeof(uint8_t));
			}
			return true;
		});

	if (bestCount == 0 || bestCount > outCap) return 0;
	memcpy(outCards, best, bestCount * sizeof(uint8_t));
	return bestCount;
}
