#include "chessAI.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cstring>

// ============================================================
// 位置价值表（红方视角，黑方 y 镜像）
// ============================================================
//
// 所有表为 9×10，从红方视角定义。
// x=0~8（左到右），y=0~9（上到下）
// 红方在 y=5~9，黑方在 y=0~4
// 红方向前 = y 减小，黑方向前 = y 增大
//
// 对黒方棋子，查表时 y 坐标映射为 9-y
// ============================================================

// ── 兵/卒位置价值 ──
// 未过河时低，过河后逐步上升，九宫附近最高
static const int8_t g_soldierPos[9][10] = {
	{ 0,  0,  0,  0,  5,  5,  2,  0,  0,  0 },
	{ 0,  0,  0,  5, 10, 10,  5,  0,  0,  0 },
	{ 0,  0,  5, 10, 20, 20, 10,  5,  5,  5 },
	{ 0,  0, 10, 20, 30, 25, 15, 10, 10, 10 },
	{ 0,  5, 15, 30, 40, 35, 20, 15, 15, 15 },
	{ 0,  0, 10, 20, 30, 25, 15, 10, 10, 10 },
	{ 0,  0,  5, 10, 20, 20, 10,  5,  5,  5 },
	{ 0,  0,  0,  5, 10, 10,  5,  0,  0,  0 },
	{ 0,  0,  0,  0,  5,  5,  2,  0,  0,  0 },
};

// ── 馬位置价值 ──
// 中心化优势，边缘蹩腿风险区域降低
static const int8_t g_horsePos[9][10] = {
	{-5, -5, -5,  0,  0,  0,  0, -5, -5, -5},
	{-5,  0,  5, 10, 10, 10, 10,  5,  0, -5},
	{ 0,  5, 15, 20, 25, 25, 20, 15,  5,  0},
	{ 0, 10, 20, 30, 35, 35, 30, 20, 10,  0},
	{ 5, 15, 25, 35, 40, 40, 35, 25, 15,  5},
	{ 0, 10, 20, 30, 35, 35, 30, 20, 10,  0},
	{ 0,  5, 15, 20, 25, 25, 20, 15,  5,  0},
	{-5,  0,  5, 10, 10, 10, 10,  5,  0, -5},
	{-5, -5, -5,  0,  0,  0,  0, -5, -5, -5},
};

// ── 車位置价值 ──
// 开扬线（无阻塞）加分，尤其是对方半场
static const int8_t g_chariotPos[9][10] = {
	{ 0,  0,  5, 10, 15, 15, 10,  5,  0,  0},
	{ 0,  5, 10, 15, 20, 20, 15, 10,  5,  0},
	{ 5, 10, 15, 20, 25, 25, 20, 15, 10,  5},
	{10, 15, 20, 25, 30, 30, 25, 20, 15, 10},
	{15, 20, 25, 30, 35, 35, 30, 25, 20, 15},
	{10, 15, 20, 25, 30, 30, 25, 20, 15, 10},
	{ 5, 10, 15, 20, 25, 25, 20, 15, 10,  5},
	{ 0,  5, 10, 15, 20, 20, 15, 10,  5,  0},
	{ 0,  0,  5, 10, 15, 15, 10,  5,  0,  0},
};

// ── 炮位置价值 ──
// 中路、有炮架潜力位置加分
static const int8_t g_cannonPos[9][10] = {
	{-5, -5,  0,  5,  5,  5,  5,  0, -5, -5},
	{-5,  0,  5, 10, 10, 10, 10,  5,  0, -5},
	{ 0,  5, 10, 15, 20, 20, 15, 10,  5,  0},
	{ 5, 10, 15, 20, 25, 25, 20, 15, 10,  5},
	{10, 15, 20, 25, 25, 25, 25, 20, 15, 10},
	{ 5, 10, 15, 20, 25, 25, 20, 15, 10,  5},
	{ 0,  5, 10, 15, 20, 20, 15, 10,  5,  0},
	{-5,  0,  5, 10, 10, 10, 10,  5,  0, -5},
	{-5, -5,  0,  5,  5,  5,  5,  0, -5, -5},
};

// ── 将/帅位置价值 ──
// 安全第一，位于九宫中心最佳
static const int8_t g_generalPos[9][10] = {
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  5,  0,  0,  5,  0,  0,  0},
	{ 0,  0,  5, 20, 10, 10, 20,  5,  0,  0},
	{ 0,  0,  0,  5,  0,  0,  5,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
};

// ── 士/仕位置价值 ──
// 在将周围防御位置最好
static const int8_t g_advisorPos[9][10] = {
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0, 15,  0,  0, 15,  0,  0,  0},
	{ 0,  0,  5,  0, 10, 10,  0,  5,  0,  0},
	{ 0,  0,  0, 15,  0,  0, 15,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
};

// ── 象/相位置价值 ──
// 连接象加分，防御位置
static const int8_t g_elephantPos[9][10] = {
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0, 10,  0,  0,  0,  0, 10,  0,  0},
	{ 0,  0,  0,  5,  0,  0,  5,  0,  0,  0},
	{ 0,  0, 10,  0, 15, 15,  0, 10,  0,  0},
	{ 0,  0,  0,  5,  0,  0,  5,  0,  0,  0},
	{ 0,  0, 10,  0,  0,  0,  0, 10,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	{ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
};

// ============================================================
// 位置价值查表
// ============================================================

int ChessAI::positionValue(ChessLogic::PieceType type, ChessLogic::Side side, int x, int y)
{
	int py = (side == ChessLogic::Side::Red) ? y : (9 - y);

	switch (type)
	{
	case ChessLogic::PieceType::Soldier:  return g_soldierPos[x][py];
	case ChessLogic::PieceType::Horse:    return g_horsePos[x][py];
	case ChessLogic::PieceType::Chariot:  return g_chariotPos[x][py];
	case ChessLogic::PieceType::Cannon:   return g_cannonPos[x][py];
	case ChessLogic::PieceType::General:  return g_generalPos[x][py];
	case ChessLogic::PieceType::Advisor:  return g_advisorPos[x][py];
	case ChessLogic::PieceType::Elephant: return g_elephantPos[x][py];
	default: return 0;
	}
}

// ============================================================
// 构造
// ============================================================

ChessAI::ChessAI()
	: m_tt(1 << 18) // 262K 条目
{
}

// ============================================================
// 子力价值
// ============================================================

int ChessAI::pieceValue(ChessLogic::PieceType type)
{
	switch (type)
	{
	case ChessLogic::PieceType::General:  return VALUE_GENERAL;
	case ChessLogic::PieceType::Chariot:  return VALUE_CHARIOT;
	case ChessLogic::PieceType::Cannon:   return VALUE_CANNON;
	case ChessLogic::PieceType::Horse:    return VALUE_HORSE;
	case ChessLogic::PieceType::Elephant: return VALUE_ELEPHANT;
	case ChessLogic::PieceType::Advisor:  return VALUE_ADVISOR;
	case ChessLogic::PieceType::Soldier:  return VALUE_SOLDIER;
	default:                              return 0;
	}
}

// ============================================================
// 评估函数
// ============================================================

int ChessAI::evaluate(const ChessLogic& logic) const
{
	ChessLogic::Side aiSide = ChessLogic::Side::Black;
	int score = 0;

	// 将的位置和防御信息
	int redKingX = -1, redKingY = -1;
	int blackKingX = -1, blackKingY = -1;
	int redDefenders = 0, blackDefenders = 0;

	for (int x = 0; x < ChessLogic::BOARD_W; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive)
				continue;

			int val = pieceValue(p.type) + positionValue(p.type, p.side, x, y);

			if (p.side == aiSide)
				score += val;
			else
				score -= val;

			// 将的位置
			if (p.type == ChessLogic::PieceType::General)
			{
				if (p.side == ChessLogic::Side::Red)
				{ redKingX = x; redKingY = y; }
				else
				{ blackKingX = x; blackKingY = y; }
			}

			// 统计将周围的防御棋子
			if (p.side == ChessLogic::Side::Red)
			{
				if (redKingX >= 0 && std::abs(x - redKingX) <= 2 && std::abs(y - redKingY) <= 2)
					redDefenders++;
			}
			else
			{
				if (blackKingX >= 0 && std::abs(x - blackKingX) <= 2 && std::abs(y - blackKingY) <= 2)
					blackDefenders++;
			}
		}
	}

	// ── 将的安全性 ──
	score += blackDefenders * KING_DEFENSE_FACTOR;  // 黑将安全加分
	score -= redDefenders * KING_DEFENSE_FACTOR;    // 红将安全减分

	// ── 残局阶段子力变化 ──
	int totalPieces = 0;
	for (int x = 0; x < ChessLogic::BOARD_W; x++)
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (!p.isEmpty() && p.alive && p.type != ChessLogic::PieceType::General)
				totalPieces++;
		}

	if (totalPieces <= 10)
	{
		for (int x = 0; x < ChessLogic::BOARD_W; x++)
		{
			for (int y = 0; y < ChessLogic::BOARD_H; y++)
			{
				const auto& p = logic.getPiece(x, y);
				if (p.isEmpty() || !p.alive)
					continue;
				if (p.type == ChessLogic::PieceType::Soldier)
				{
					int bonus = 50;
					score += (p.side == aiSide) ? bonus : -bonus;
				}
				if (p.type == ChessLogic::PieceType::Horse)
				{
					int bonus = 100;
					score += (p.side == aiSide) ? bonus : -bonus;
				}
			}
		}
	}

	return score;
}

// ============================================================
// 走法排序分（MVV-LVA）
// ============================================================

int ChessAI::moveScore(const ChessLogic& logic, int fromX, int fromY, int toX, int toY) const
{
	const auto& captured = logic.getPiece(toX, toY);
	if (captured.isEmpty() || !captured.alive)
		return 0;

	int victimValue = pieceValue(captured.type);
	int attackerValue = pieceValue(logic.getPiece(fromX, fromY).type);
	return victimValue * 10 - attackerValue;
}

// ============================================================
// 收集走法并排序
// ============================================================

void ChessAI::getOrderedMoves(ChessLogic& logic, ChessLogic::Side side,
							  uint32_t ttBestEnc, int ply,
							  std::vector<ChessLogic::Move>& moves)
{
	moves.clear();

	for (int x = 0; x < ChessLogic::BOARD_W; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive || p.side != side)
				continue;

			auto positions = logic.getValidMoves(x, y);
			for (const auto& pos : positions)
			{
				ChessLogic::Move m;
				m.from = { static_cast<int8_t>(x), static_cast<int8_t>(y) };
				m.to = pos;
				m.captured = logic.getPiece(pos.x, pos.y);
				moves.push_back(m);
			}
		}
	}

	if (moves.empty())
		return;

	struct ScoredMove {
		ChessLogic::Move move;
		int sortScore;
	};

	std::vector<ScoredMove> scored;
	scored.reserve(moves.size());

	uint32_t ttFromEnc = (ttBestEnc >> 16) & 0xFFFF;
	uint32_t ttToEnc = ttBestEnc & 0xFFFF;

	for (const auto& m : moves)
	{
		int sc = 0;
		uint32_t enc = moveEncode(m);

		// TT best move → 最高优先级
		if (ttBestEnc != 0 && (enc >> 16) == ttFromEnc && (enc & 0xFFFF) == ttToEnc)
		{
			sc = 1000000;
		}
		else
		{
			// 吃子分数（MVV-LVA）
			sc = moveScore(logic, m.from.x, m.from.y, m.to.x, m.to.y);
			if (sc > 0)
			{
				sc += 1000;
			}
			else
			{
				// 非吃子走法 → 检查是否杀手走法
				for (int k = 0; k < KILLER_SLOTS; k++)
				{
					if (m_killers[ply][k] == enc)
					{
						sc = 900 - k * 100;
						break;
					}
				}
			}
		}

		scored.push_back({ m, sc });
	}

	std::sort(scored.begin(), scored.end(),
		[](const ScoredMove& a, const ScoredMove& b) { return a.sortScore > b.sortScore; });

	for (size_t i = 0; i < scored.size(); i++)
		moves[i] = scored[i].move;
}

// ============================================================
// 静态搜索（Quiescence Search）
// ============================================================

int ChessAI::quiescence(ChessLogic& logic, int alpha, int beta,
						ChessLogic::Side side, int ply)
{
	if (isTimeout())
		return 0;

	if (logic.getState() == ChessLogic::State::GameOver)
	{
		auto winner = logic.getWinner();
		if (winner == ChessLogic::Side::Black)  return 99999 - ply * 10;
		if (winner == ChessLogic::Side::Red)    return -99999 + ply * 10;
		return 0;
	}

	bool inCheck = logic.isInCheck(side);
	int standPat = evaluate(logic);

	if (!inCheck)
	{
		if (standPat >= beta)  return beta;
		if (standPat > alpha)  alpha = standPat;
	}

	struct CaptureMove {
		int fromX, fromY, toX, toY;
		int score;
	};
	std::vector<CaptureMove> captures;

	for (int x = 0; x < ChessLogic::BOARD_W; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive || p.side != side)
				continue;

			auto positions = logic.getValidMoves(x, y);
			for (const auto& pos : positions)
			{
				const auto& target = logic.getPiece(pos.x, pos.y);
				if (target.isEmpty() || !target.alive)
				{
					if (!inCheck)
						continue;
				}
				int sc = moveScore(logic, x, y, pos.x, pos.y);
				captures.push_back({ x, y, pos.x, pos.y, sc });
			}
		}
	}

	std::sort(captures.begin(), captures.end(),
		[](const CaptureMove& a, const CaptureMove& b) { return a.score > b.score; });

	bool isMaximizing = (side == ChessLogic::Side::Black);

	for (const auto& c : captures)
	{
		bool moved = logic.movePiece(c.fromX, c.fromY, c.toX, c.toY);
		if (!moved) continue;

		ChessLogic::Side nextSide = logic.getCurrentTurn();
		int eval = quiescence(logic, alpha, beta, nextSide, ply + 1);
		logic.undoMove();

		if (isMaximizing)
		{
			if (eval > alpha) alpha = eval;
			if (alpha >= beta) return beta;
		}
		else
		{
			if (eval < beta) beta = eval;
			if (alpha >= beta) return alpha;
		}
	}

	return isMaximizing ? alpha : beta;
}

// ============================================================
// Alpha-Beta 搜索
// ============================================================

int ChessAI::alphaBeta(ChessLogic& logic, int depth, int alpha, int beta,
					   ChessLogic::Side side, int ply, bool)
{
	if (isTimeout())
		return 0;

	if (logic.getState() == ChessLogic::State::GameOver)
	{
		auto winner = logic.getWinner();
		if (winner == ChessLogic::Side::Black)  return 99999 - ply * 10;
		if (winner == ChessLogic::Side::Red)    return -99999 + ply * 10;
		return 0;
	}

	bool inCheck = logic.isInCheck(side);

	// ── 置换表探测 ──
	int16_t ttScore = 0;
	uint8_t ttBestFrom = 0, ttBestTo = 0;
	uint32_t ttBestEnc = 0;
	bool ttHit = m_tt.probe(logic.getHash(), depth, alpha, beta, ttScore, ttBestFrom, ttBestTo);

	if (ttBestFrom != 0 || ttBestTo != 0)
		ttBestEnc = (static_cast<uint32_t>(ttBestFrom) << 16) | ttBestTo;

	if (ttHit)
		return ttScore;

	// ── 达到搜索深度 → 静态搜索 ──
	if (depth <= 0)
		return quiescence(logic, alpha, beta, side, ply);

	// ── 收集并排序走法 ──
	std::vector<ChessLogic::Move> moves;
	getOrderedMoves(logic, side, ttBestEnc, ply, moves);

	if (moves.empty())
	{
		if (inCheck)
			return (side == ChessLogic::Side::Black) ? (-99999 + ply * 10) : (99999 - ply * 10);
		else
			return (side == ChessLogic::Side::Black) ? (-99999 + ply * 10) : (99999 - ply * 10);
	}

	// ── 搜索 ──
	bool isMaximizing = (side == ChessLogic::Side::Black);
	int bestScore = isMaximizing ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
	uint32_t bestMoveEnc = 0;

	for (const auto& m : moves)
	{
		uint32_t enc = moveEncode(m);
		bool moved = logic.movePiece(m.from.x, m.from.y, m.to.x, m.to.y);
		if (!moved) continue;

		ChessLogic::Side nextSide = logic.getCurrentTurn();
		int eval;

		if (isMaximizing)
		{
			eval = alphaBeta(logic, depth - 1, alpha, beta, nextSide, ply + 1, true);
			if (eval > bestScore)
			{
				bestScore = eval;
				bestMoveEnc = enc;
			}
			if (eval > alpha) alpha = eval;
		}
		else
		{
			eval = alphaBeta(logic, depth - 1, alpha, beta, nextSide, ply + 1, true);
			if (eval < bestScore)
			{
				bestScore = eval;
				bestMoveEnc = enc;
			}
			if (eval < beta) beta = eval;
		}

		logic.undoMove();

		if (alpha >= beta)
		{
			// Beta 剪枝 → 保存杀手走法（非吃子走法）
			if (moveScore(logic, m.from.x, m.from.y, m.to.x, m.to.y) <= 0)
			{
				if (m_killers[ply][KILLER_SLOTS - 1] != enc)
				{
					m_killers[ply][1] = m_killers[ply][0];
					m_killers[ply][0] = enc;
				}
			}
			break;
		}
	}

	// ── 保存到置换表 ──
	TranspositionTable::Flag flag = TranspositionTable::EXACT;
	if (isMaximizing)
	{
		if (bestScore <= alpha) flag = TranspositionTable::UPPER_BOUND;
		if (bestScore >= beta)  flag = TranspositionTable::LOWER_BOUND;
	}
	else
	{
		if (bestScore >= beta)  flag = TranspositionTable::UPPER_BOUND;
		if (bestScore <= alpha) flag = TranspositionTable::LOWER_BOUND;
	}

	uint8_t bestFrom = (bestMoveEnc >> 16) & 0xFF;
	uint8_t bestTo = bestMoveEnc & 0xFF;
	m_tt.store(logic.getHash(), depth, static_cast<int16_t>(bestScore), flag, bestFrom, bestTo);

	return bestScore;
}

// ============================================================
// 主入口
// ============================================================

ChessLogic::Move ChessAI::getMove(ChessLogic& logic)
{
	ChessLogic::Move bestMove;
	bestMove.from = { -1, -1 };

	ChessLogic::Side aiSide = logic.getCurrentTurn();

	// ── 开局库优先 ──
	std::vector<OpeningBook::BookMove> bookMoves;
	m_book.getBookMoves(logic.getHistory(), bookMoves);

	if (!bookMoves.empty())
	{
		for (const auto& bm : bookMoves)
		{
			int fromX = bm.fromEnc / 10;
			int fromY = bm.fromEnc % 10;
			int toX = bm.toEnc / 10;
			int toY = bm.toEnc % 10;

			auto moves = logic.getValidMoves(fromX, fromY);
			for (const auto& m : moves)
			{
				if (m.x == toX && m.y == toY)
				{
					bestMove.from = { static_cast<int8_t>(fromX), static_cast<int8_t>(fromY) };
					bestMove.to = { static_cast<int8_t>(toX), static_cast<int8_t>(toY) };
					bestMove.captured = logic.getPiece(toX, toY);
					ESP_LOGI("ChessAI", "开局库: (%d,%d)->(%d,%d)", fromX, fromY, toX, toY);
					return bestMove;
				}
			}
		}
	}

	// ── 清空搜索状态 ──
	memset(m_killers, 0, sizeof(m_killers));
	m_searchStartUs = esp_timer_get_time();
	m_timeout = false;
	m_nodesSearched = 0;

	// ── 迭代加深 ──
	int alpha = std::numeric_limits<int>::min();
	int beta = std::numeric_limits<int>::max();
	int completedDepth = 0;
	int bestScore = 0;

	for (int depth = 1; depth <= MAX_DEPTH; depth++)
	{
		if (m_timeout)
			break;

		bestScore = alphaBeta(logic, depth, alpha, beta, aiSide, 0, true);

		if (m_timeout)
			break;

		completedDepth = depth;

		if (bestScore > 99000 || bestScore < -99000)
			break;

		uint64_t elapsed = (esp_timer_get_time() - m_searchStartUs) / 1000;
		if (elapsed > static_cast<uint64_t>(TIME_LIMIT_MS))
			break;
	}

	// ── 从置换表提取最佳走法 ──
	uint64_t rootHash = logic.getHash();
	int16_t ttScore = 0;
	uint8_t ttBestFrom = 0, ttBestTo = 0;
	// 用 depth=MAX_DEPTH 探测（获取 TT 中根节点的最佳走法）
	m_tt.probe(rootHash, MAX_DEPTH, alpha, beta, ttScore, ttBestFrom, ttBestTo);

	ESP_LOGI("ChessAI", "搜索完成: 深度=%d, 节点=%d, 耗时=%llu ms, bestFrom=%d bestTo=%d",
		completedDepth, m_nodesSearched,
		(esp_timer_get_time() - m_searchStartUs) / 1000,
		ttBestFrom, ttBestTo);

	if (ttBestFrom != 0 || ttBestTo != 0)
	{
		int fromX = ttBestFrom / 10;
		int fromY = ttBestFrom % 10;
		int toX = ttBestTo / 10;
		int toY = ttBestTo % 10;

		if (fromX >= 0 && fromX < 9 && fromY >= 0 && fromY < 10 &&
			toX >= 0 && toX < 9 && toY >= 0 && toY < 10)
		{
			bestMove.from = { static_cast<int8_t>(fromX), static_cast<int8_t>(fromY) };
			bestMove.to = { static_cast<int8_t>(toX), static_cast<int8_t>(toY) };
			bestMove.captured = logic.getPiece(toX, toY);
			return bestMove;
		}
	}

	// ── 兜底：搜索第一个合法走法 ──
	for (int x = 0; x < ChessLogic::BOARD_W && bestMove.from.x < 0; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H && bestMove.from.x < 0; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive || p.side != aiSide)
				continue;
			auto positions = logic.getValidMoves(x, y);
			if (!positions.empty())
			{
				bestMove.from = { static_cast<int8_t>(x), static_cast<int8_t>(y) };
				bestMove.to = positions[0];
				bestMove.captured = logic.getPiece(positions[0].x, positions[0].y);
			}
		}
	}

	return bestMove;
}
