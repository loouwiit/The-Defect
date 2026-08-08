#pragma once

#include <cstdint>
#include "../douDiZhuLogic.hpp"

/**
 * @brief 斗地主联机协议（host-authoritative + 隐私隔离）
 *
 * 传输：TCP，\n 分隔的 JSON 文本帧（同 tetrisTcpHost 行缓冲）。
 * 端口：从机 listen 8086，主机主动连接（反客为主，同 tetrisRemoteClient）。
 *
 * 隐私保证：主机绝不向某客户端发送其他玩家手牌，
 * 只发送该客户端自己的手牌（deal / hand）+ 公开桌面信息。
 */

namespace DouDizhuMsg {

inline constexpr uint16_t CLIENT_PORT = 8086;   // 从机监听端口（主机来连）

// H→C
inline constexpr const char* WELCOME     = "welcome";     // {"type":"welcome","seat":1,"player_count":3}
inline constexpr const char* GAME_START  = "game_start";  // {"type":"game_start","dealer":0}
inline constexpr const char* DEAL        = "deal";        // {"type":"deal","cards":[...]} — 仅自己手牌
inline constexpr const char* BID_TURN    = "bid_turn";    // {"type":"bid_turn","seat":1,"min_bid":1,"max_bid":0,"max_bidder":-1}
inline constexpr const char* BID         = "bid";         // {"type":"bid","seat":0,"bid":2} bid=0 不叫
inline constexpr const char* LANDLORD    = "landlord";    // {"type":"landlord","seat":1,"bid":3,"bottom":[..]} 底牌公开
inline constexpr const char* TURN        = "turn";        // {"type":"turn","seat":2}
inline constexpr const char* PLAY        = "play";        // {"type":"play","seat":0,"cards":[...],"pass":false,"new_trick":true,"counts":[17,15,17]}
inline constexpr const char* HAND        = "hand";        // {"type":"hand","cards":[...]} — 仅自己出牌后手牌
inline constexpr const char* GAME_OVER   = "game_over";   // {"type":"game_over","landlord_win":true,"multiplier":8,"scores":[...],"delta":[...],"spring":false,"reverse_spring":false}
inline constexpr const char* REMATCH     = "rematch";     // {"type":"rematch"} 新一局（随后 game_start + deal）
inline constexpr const char* ABORT       = "abort";       // {"type":"abort","reason":"..."} 中断返回大厅

// C→H（同 type 名称，方向不同）
// {"type":"bid","value":2}   value 0=不叫
// {"type":"play","cards":[...]}
// {"type":"pass"}
// {"type":"rematch"}

} // namespace DouDizhuMsg

/**
 * @brief 从机可渲染视图（只含本机手牌 + 公开信息）
 *
 * 由 DouDizhuRemoteClient 的接收任务写入，App 侧读取快照。
 */
struct DouDizhuClientView
{
	int seat = -1;
	int playerCount = 0;
	int phase = 0;             // 0=等待 1=叫分 2=出牌 3=结算

	// ── 手牌（私有，仅本机） ──
	uint8_t hand[DouDizhuLogic::MAX_HAND]{};
	int handCount = 0;

	// ── 叫分（公开） ──
	int dealer = 0;
	int bidTurn = -1;
	int minBid = 1;
	int maxBid = 0;
	int maxBidder = -1;
	int bidValue = 0;
	int landlord = -1;

	// ── 出牌（公开） ──
	int turn = -1;
	int lastPlaySeat = -1;
	uint8_t trickCards[3][DouDizhuLogic::MAX_HAND]{};
	int trickCount[3] = {};
	bool trickPass[3] = {};
	int cardCount[3] = { 17, 17, 17 };

	// ── 底牌（地主确定后公开） ──
	uint8_t bottom[DouDizhuLogic::BOTTOM_SIZE]{};
	bool bottomRevealed = false;

	// ── 结算 ──
	bool landlordWin = false;
	int multiplier = 1;
	int scores[3] = {};
	int delta[3] = {};
	bool spring = false;
	bool reverseSpring = false;

	// ── 我方是否轮到 ──
	bool myBidTurn() const { return phase == 1 && bidTurn == seat; }
	bool myPlayTurn() const { return phase == 2 && turn == seat; }
	bool myMustLead() const {
		if (phase != 2 || turn != seat) return false;
		if (lastPlaySeat < 0) return true;
		return lastPlaySeat == seat;
	}
	bool canPass() const {
		if (phase != 2 || turn != seat) return false;
		if (lastPlaySeat < 0) return false;
		return lastPlaySeat != seat;
	}
};
