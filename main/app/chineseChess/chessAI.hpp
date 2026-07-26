#pragma once

#include "chessLogic.hpp"
#include "transpositionTable.hpp"
#include "openingBook.hpp"
#include "esp_timer.h"
#include <algorithm>
#include <limits>
#include <cstdint>

/**
 * @brief 中国象棋 AI 引擎 v2
 *
 * 核心技术：
 *   - 迭代加深（Iterative Deepening）+ 时间控制（~800ms）
 *   - Alpha-Beta 剪枝 + 置换表（Transposition Table）
 *   - 杀手启发式（Killer Heuristic）
 *   - 空着剪枝（Null Move Pruning）
 *   - 高质量评估：子力 + 位置价值表 + 活动性 + 将安全性 + 兵种配合
 *   - 开局库优先
 */
class ChessAI
{
public:
	ChessAI();
	~ChessAI() = default;

	/**
	 * @brief 获取 AI 走法
	 * @param logic 当前棋盘逻辑
	 * @return AI 选择的走法
	 */
	ChessLogic::Move getMove(ChessLogic& logic);

private:
	// ── 搜索参数 ──
	static constexpr int   MAX_DEPTH       = 8;
	static constexpr int   NULL_MOVE_R     = 2;     // 空着剪枝缩减深度
	static constexpr int   MIN_NULL_DEPTH  = 3;     // 启用空着剪枝的最小深度
	static constexpr int   KILLER_SLOTS    = 2;     // 每层杀手走法数
	static constexpr int   TIME_LIMIT_MS   = 600;   // 搜索时间目标
	static constexpr int   TIME_MAX_MS     = 1200;  // 搜索时间上限

	// ── 子力基础价值 ──
	static constexpr int VALUE_GENERAL  = 100000;
	static constexpr int VALUE_CHARIOT  = 900;
	static constexpr int VALUE_CANNON   = 450;
	static constexpr int VALUE_HORSE    = 400;
	static constexpr int VALUE_ELEPHANT = 200;
	static constexpr int VALUE_ADVISOR  = 200;
	static constexpr int VALUE_SOLDIER  = 100;

	// ── 活动性/配合加成系数 ──
	static constexpr int MOBILITY_FACTOR     = 4;    // 每多一个合法走法加分
	static constexpr int KING_DEFENSE_FACTOR = 15;   // 将周围防御棋子加分
	static constexpr int THREAT_FACTOR       = 8;    // 威胁对方高分值棋子加分

	// ── 组件 ──
	TranspositionTable m_tt;
	OpeningBook        m_book;

	// 杀手走法表：m_killers[ply][slot]，ply = MAX_DEPTH - depth
	// 值编码：fromEnc << 16 | toEnc
	uint32_t m_killers[MAX_DEPTH][KILLER_SLOTS]{};

	// 时间追踪
	uint64_t m_searchStartUs = 0;  // esp_timer_get_time() 起始
	bool     m_timeout = false;

	// 搜索统计
	int      m_nodesSearched = 0;

	// ═════════════════════════════════════════════════════════
	// 评估函数
	// ═════════════════════════════════════════════════════════

	/** @brief 获取棋子基础价值 */
	static int pieceValue(ChessLogic::PieceType type);

	/** @brief 位置价值表评估 */
	static int positionValue(ChessLogic::PieceType type, ChessLogic::Side side, int x, int y);

	/** @brief 主评估函数 */
	int evaluate(const ChessLogic& logic) const;

	// ═════════════════════════════════════════════════════════
	// 搜索
	// ═════════════════════════════════════════════════════════

	/** @brief Alpha-Beta 搜索（含空着剪枝、置换表、杀手启发式） */
	int alphaBeta(ChessLogic& logic, int depth, int alpha, int beta,
				  ChessLogic::Side side, int ply, bool canNullMove);

	/** @brief 静态搜索（只吃子走法，解决地平线效应） */
	int quiescence(ChessLogic& logic, int alpha, int beta,
				   ChessLogic::Side side, int ply);

	/**
	 * @brief 收集走法并排序
	 * @param logic 棋盘
	 * @param side 走棋方
	 * @param ttBestEnc TT 中的最佳走法编码
	 * @param ply 当前层
	 * @param[out] moves 走法列表
	 */
	void getOrderedMoves(ChessLogic& logic, ChessLogic::Side side,
						 uint32_t ttBestEnc, int ply,
						 std::vector<ChessLogic::Move>& moves);

	/** @brief 走法 MVV-LVA 排序分 */
	int moveScore(const ChessLogic& logic, int fromX, int fromY, int toX, int toY) const;

	/** @brief 检查是否超时 */
	inline bool isTimeout()
	{
		if (m_timeout) return true;
		if (m_nodesSearched++ % 1024 == 0)
		{
			uint64_t elapsed = esp_timer_get_time() - m_searchStartUs;
			if (elapsed > static_cast<uint64_t>(TIME_MAX_MS) * 1000)
				m_timeout = true;
		}
		return m_timeout;
	}

	/** @brief 编码走法 (fromEnc << 16 | toEnc) */
	static uint32_t moveEncode(int fromX, int fromY, int toX, int toY)
	{
		return (static_cast<uint32_t>(fromX * 10 + fromY) << 16)
			 | static_cast<uint32_t>(toX * 10 + toY);
	}

	/** @brief 编码走法（用 Move 结构） */
	static uint32_t moveEncode(const ChessLogic::Move& m)
	{
		return moveEncode(m.from.x, m.from.y, m.to.x, m.to.y);
	}
};
