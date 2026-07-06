#include "chessAI.hpp"
#include <cstdlib>
#include <vector>
#include <algorithm>

// ============================================================
// 构造
// ============================================================

ChessAI::ChessAI(int depth)
	: m_depth{ depth }
{
}

// ============================================================
// 子力价值
// ============================================================

int ChessAI::pieceValue(ChessLogic::PieceType type) const
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
// 位置价值
// ============================================================

int ChessAI::positionBonus(ChessLogic::PieceType type, ChessLogic::Side side, int x, int y) const
{
	// ── 兵/卒：过河后给予额外价值 ──
	if (type == ChessLogic::PieceType::Soldier)
	{
		bool crossed = !ChessLogic::onOwnHalf(x, y, side);
		if (crossed)
		{
			// 过河后 +30，进入敌方九宫两侧再加 20
			int bonus = 30;
			if (x >= 3 && x <= 5)
				bonus += 20;
			return bonus;
		}
		return 0;
	}

	// ── 馬：中心化奖励 ──
	if (type == ChessLogic::PieceType::Horse)
	{
		// 越靠近棋盘中心，活动范围越大
		int centerDist = std::abs(x - 4) + std::abs(y - 4);
		return std::max(0, (8 - centerDist) * 5);
	}

	// ── 車：占据中路优势 ──
	if (type == ChessLogic::PieceType::Chariot)
	{
		if (x >= 3 && x <= 5)
			return 15;
		return 0;
	}

	// ── 炮：有炮架位置时更有威力 ──
	if (type == ChessLogic::PieceType::Cannon)
	{
		if (x >= 3 && x <= 5)
			return 10;
		return 0;
	}

	return 0;
}

// ============================================================
// 评估函数
// ============================================================

int ChessAI::evaluate(const ChessLogic& logic) const
{
	int score = 0;
	ChessLogic::Side aiSide = ChessLogic::Side::Black; // AI 固定执黑

	for (int x = 0; x < ChessLogic::BOARD_W; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive)
				continue;

			int val = pieceValue(p.type) + positionBonus(p.type, p.side, x, y);
			if (p.side == aiSide)
				score += val;   // AI 方（黑）：正分
			else
				score -= val;   // 对手方（红）：负分
		}
	}

	return score;
}

// ============================================================
// 走法排序分（MVV-LVA：吃价值高的子排前面）
// ============================================================

int ChessAI::moveScore(const ChessLogic& logic, int fromX, int fromY, int toX, int toY) const
{
	const auto& captured = logic.getPiece(toX, toY);
	if (captured.isEmpty() || !captured.alive)
		return 0; // 非吃子走法

	// 被吃子价值 - 攻击子价值/10 作为次级排序
	int victimValue = pieceValue(captured.type);
	int attackerValue = pieceValue(logic.getPiece(fromX, fromY).type);
	return victimValue * 10 - attackerValue;
}

// ============================================================
// Minimax + Alpha-Beta
// ============================================================

int ChessAI::minimax(ChessLogic& logic, int depth, int alpha, int beta, ChessLogic::Side side) const
{
	// ── 终端节点：将死 → 极值评估 ──
	if (logic.getState() == ChessLogic::State::GameOver)
	{
		ChessLogic::Side winner = logic.getWinner();
		if (winner == ChessLogic::Side::Black)
			return 99999 - (m_depth - depth) * 100; // 越快赢分越高
		else if (winner == ChessLogic::Side::Red)
			return -99999 + (m_depth - depth) * 100;
		return 0;
	}

	// ── 达到搜索深度 → 静态评估 ──
	if (depth <= 0)
		return evaluate(logic);

	// ── 收集所有走法并排序 ──
	struct MoveCandidate
	{
		int fromX, fromY, toX, toY;
		int score;
	};
	std::vector<MoveCandidate> candidates;

	for (int x = 0; x < ChessLogic::BOARD_W; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive || p.side != side)
				continue;

			auto moves = logic.getValidMoves(x, y);
			for (const auto& m : moves)
			{
				int sc = moveScore(logic, x, y, m.x, m.y);
				candidates.push_back({ x, y, m.x, m.y, sc });
			}
		}
	}

	if (candidates.empty())
		return evaluate(logic); // 无合法走法，困毙

	// 按走法排序分降序排列（吃子走法优先，提高剪枝效率）
	std::sort(candidates.begin(), candidates.end(),
		[](const MoveCandidate& a, const MoveCandidate& b) { return a.score > b.score; });

	// ── 搜索 ──
	bool isMaximizing = (side == ChessLogic::Side::Black); // AI 最大化

	if (isMaximizing)
	{
		int maxEval = std::numeric_limits<int>::min();
		for (const auto& c : candidates)
		{
			bool moved = logic.movePiece(c.fromX, c.fromY, c.toX, c.toY);
			if (!moved) continue;

			ChessLogic::Side nextSide = logic.getCurrentTurn();
			int eval = minimax(logic, depth - 1, alpha, beta, nextSide);

			logic.undoMove();

			maxEval = std::max(maxEval, eval);
			alpha = std::max(alpha, eval);
			if (beta <= alpha)
				break; // Beta 剪枝
		}
		return maxEval;
	}
	else
	{
		int minEval = std::numeric_limits<int>::max();
		for (const auto& c : candidates)
		{
			bool moved = logic.movePiece(c.fromX, c.fromY, c.toX, c.toY);
			if (!moved) continue;

			ChessLogic::Side nextSide = logic.getCurrentTurn();
			int eval = minimax(logic, depth - 1, alpha, beta, nextSide);

			logic.undoMove();

			minEval = std::min(minEval, eval);
			beta = std::min(beta, eval);
			if (beta <= alpha)
				break; // Alpha 剪枝
		}
		return minEval;
	}
}

// ============================================================
// 主入口
// ============================================================

ChessLogic::Move ChessAI::getMove(ChessLogic& logic)
{
	ChessLogic::Move bestMove;
	bestMove.from = { -1, -1 };

	ChessLogic::Side aiSide = logic.getCurrentTurn();

	// 收集所有走法并排序
	struct Candidate
	{
		int fromX, fromY;
		ChessLogic::Position to;
		int score;
	};
	std::vector<Candidate> candidates;

	for (int x = 0; x < ChessLogic::BOARD_W; x++)
	{
		for (int y = 0; y < ChessLogic::BOARD_H; y++)
		{
			const auto& p = logic.getPiece(x, y);
			if (p.isEmpty() || !p.alive || p.side != aiSide)
				continue;

			auto moves = logic.getValidMoves(x, y);
			for (const auto& m : moves)
			{
				int sc = moveScore(logic, x, y, m.x, m.y);
				candidates.push_back({ x, y, m, sc });
			}
		}
	}

	if (candidates.empty())
		return bestMove;

	// 按排序分降序排列（初始走法顺序好 → 剪枝更高效）
	std::sort(candidates.begin(), candidates.end(),
		[](const Candidate& a, const Candidate& b) { return a.score > b.score; });

	// ── Minimax 搜索 ──
	int alpha = std::numeric_limits<int>::min();
	int beta = std::numeric_limits<int>::max();
	int bestScore = std::numeric_limits<int>::min();

	for (const auto& c : candidates)
	{
		bool moved = logic.movePiece(c.fromX, c.fromY, c.to.x, c.to.y);
		if (!moved) continue;

		ChessLogic::Side nextSide = logic.getCurrentTurn();
		int score = minimax(logic, m_depth - 1, alpha, beta, nextSide);

		logic.undoMove();

		if (score > bestScore)
		{
			bestScore = score;
			bestMove.from = { static_cast<int8_t>(c.fromX), static_cast<int8_t>(c.fromY) };
			bestMove.to = c.to;
			bestMove.captured = logic.getPiece(c.to.x, c.to.y);
		}

		alpha = std::max(alpha, score);
		if (beta <= alpha)
			break;
	}

	return bestMove;
}
