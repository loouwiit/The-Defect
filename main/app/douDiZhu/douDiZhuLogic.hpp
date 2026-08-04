#pragma once

#include <cstdint>
#include <functional>

/**
 * @brief 斗地主纯逻辑层 — 无 LVGL / 无网络依赖
 *
 * 牌编码：
 *   - 0..51  普通牌，id/4 = 点数，id%4 = 花色
 *   - 52 = 小王，53 = 大王
 * 点数 rank 0..14：0=3, 1=4, ..., 8=J, 9=Q, 10=K, 11=A, 12=2, 13=小王, 14=大王
 */
class DouDizhuLogic
{
public:
	static constexpr int DECK_SIZE   = 54;
	static constexpr int HAND_SIZE   = 17;
	static constexpr int BOTTOM_SIZE = 3;
	static constexpr int MAX_PLAYERS = 3;
	static constexpr int MAX_HAND    = 20;   // 17 + 3 底牌

	// ── 牌基础 ──
	static int cardRank(uint8_t card);         // 0..14
	static int cardSuit(uint8_t card);         // 0..3，王 = -1
	static bool isRed(uint8_t card);           // 红桃/方块
	static const char* rankText(uint8_t card);      // "3".."2", "小王", "大王"
	static const char* suitSymbol(uint8_t card);    // "♠""♥""♣""♦"（王返回 ""）

	// ── 洗牌发牌 ──
	static void shuffleDeck(uint8_t deck[DECK_SIZE]);
	/// 发牌：hands[3][20] + counts + bottom[3]，返回后手牌已排序
	static void deal(const uint8_t deck[DECK_SIZE],
	                 uint8_t hands[3][MAX_HAND], int counts[3], uint8_t bottom[BOTTOM_SIZE]);
	/// 手牌按点数升序排序（同点按花色）
	static void sortHand(uint8_t* cards, int count);

	// ── 牌型 ──
	struct Combo
	{
		enum class Type : uint8_t {
			None = 0, Single, Pair, Triple, TripleSingle, TriplePair,
			Straight, PairStraight, Plane, PlaneSingle, PlanePair,
			FourTwo, FourTwoPair, Bomb, Rocket
		};
		Type type = Type::None;
		int  mainRank = 0;   // 主牌点（比较用）
		int  length = 0;     // 顺子/连对/飞机长度
		int  cardCount = 0;  // 总张数
	};

	static Combo analyze(const uint8_t* cards, int count);
	/// cur 能否压过 prev（prev.type==None 表示先手任意合法牌）
	static bool canBeat(const Combo& cur, const Combo& prev);
	static bool isValidPlay(const uint8_t* cards, int count, const Combo& prev);
	static const char* comboName(Combo::Type t);

	/// 枚举手牌中所有能压过 prev（或先手任意）的出法；emit 返回 false 提前终止
	static void enumeratePlays(const uint8_t* hand, int count, const Combo& prev,
	                           const std::function<bool(const uint8_t*, int)>& emit);

	/// 提示：挑一个最省张且主牌最小的合法出法；返回张数（0 = 无解/应不出）
	static int findHint(const uint8_t* hand, int count, const Combo& prev,
	                    uint8_t* outCards, int outCap);
};

/**
 * @brief 斗地主对局状态机（host 权威 + 单机共用）
 *
 * 持有全部 3 手牌（host 侧），渲染层只允许读取本机座位。
 */
class DouDizhuGame
{
public:
	enum class Phase : uint8_t { Waiting, Bidding, Playing, Settle };
	enum class BidResult : uint8_t { Continue, Landlord, Redeal };

	Phase phase = Phase::Waiting;
	int   dealer = 0;                       // 先叫分座位

	// ── 手牌（内部持有全部，渲染只读本机） ──
	uint8_t hands[3][DouDizhuLogic::MAX_HAND];
	int     handCount[3] = { 0, 0, 0 };
	uint8_t bottom[DouDizhuLogic::BOTTOM_SIZE];

	// ── 叫分 ──
	int  bidTurn = -1;
	int  maxBid = 0;
	int  maxBidder = -1;
	bool passed[3] = {};      // 本轮已"不叫"
	bool bidDone[3] = {};     // 本轮已行动（叫分或不叫）
	int  bidValue = 0;
	int  landlord = -1;

	// ── 出牌 ──
	int  turn = -1;
	int  lastPlaySeat = -1;
	DouDizhuLogic::Combo lastCombo;
	uint8_t trickCards[3][DouDizhuLogic::MAX_HAND];
	int     trickCount[3] = {};
	bool    trickPass[3] = {};

	// ── 倍数 / 结算 ──
	int  multiplier = 1;
	int  farmerPlays = 0;      // 农民总出牌次数（春天检测）
	int  landlordPlays = 0;    // 地主总出牌次数（反春检测）
	int  scores[3] = { 0, 0, 0 };
	int  delta[3] = {};
	bool landlordWin = false;
	bool spring = false;
	bool reverseSpring = false;

	// ── 操作 ──
	void startGame(int newDealer);                 // 洗牌发牌 → Bidding
	BidResult applyBid(int seat, int value);       // value: 0=不叫, 1..3=叫分
	bool applyPlay(int seat, const uint8_t* cards, int count);  // 合法则执行并返回 true
	void applyPass(int seat);
	/// 本座位当前是否必须出牌（不能不出）
	bool mustLead(int seat) const;
	/// 结算（手牌清空时调用）
	void finish();

	// ── 辅助 ──
	bool hasCard(int seat, uint8_t card) const;
	bool removeCards(int seat, const uint8_t* cards, int count);
	int  nextActive(int seat) const;               // 下一个未出局座位（简易：顺序）
	int  seatNext(int s) const { return (s + 1) % 3; }
	int  seatPrev(int s) const { return (s + 2) % 3; }

private:
	/// 定地主后进入出牌阶段（地主拿底牌）
	void enterPlaying();
};
