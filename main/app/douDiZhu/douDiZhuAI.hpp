#pragma once

#include <cstdint>
#include "douDiZhuLogic.hpp"

/**
 * @brief 斗地主 AI — 叫分 + 出牌启发式
 *
 * 用于单机模式（1 人 + 2 AI）。仅基于本机手牌与公开信息决策，
 * 不读取其他玩家手牌（与"各主机只能看到自己的牌"一致）。
 */
class DouDizhuAI
{
public:
	/// @param minBid  当前可叫的最低分（当前最高叫分 + 1）
	/// @param mustBid 只剩自己未叫，必须当地主
	/// @return 0 = 不叫，1..3 = 叫分
	static int chooseBid(const uint8_t* hand, int count, int minBid, bool mustBid);

	/// @return 出牌张数；0 = 不出
	static int choosePlay(const uint8_t* hand, int count,
	                      const DouDizhuLogic::Combo& prev, int prevSeat,
	                      bool mustLead, int mySeat, int landlord,
	                      uint8_t* outCards, int outCap);
};
