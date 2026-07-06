#pragma once

#include "chessLogic.hpp"
#include <algorithm>
#include <limits>

/**
 * @brief 中国象棋 AI 引擎（Minimax + Alpha-Beta 剪枝）
 *
 * 策略：
 *   - 搜索深度 3 层（含 Alpha-Beta 剪枝）
 *   - 评估函数 = 子力价值 + 位置价值
 *   - 走法排序：吃子优先（MVV-LVA），提高剪枝效率
 */
class ChessAI
{
public:
	ChessAI(int depth = 3);
	~ChessAI() = default;

	/**
	 * @brief 获取 AI 走法
	 * @param logic 当前棋盘逻辑
	 * @return AI 选择的走法
	 */
	ChessLogic::Move getMove(ChessLogic& logic);

private:
	int m_depth;

	// ── 子力基础价值 ──
	static constexpr int VALUE_GENERAL = 100000;
	static constexpr int VALUE_CHARIOT = 900;
	static constexpr int VALUE_CANNON  = 450;
	static constexpr int VALUE_HORSE   = 400;
	static constexpr int VALUE_ELEPHANT = 200;
	static constexpr int VALUE_ADVISOR = 200;
	static constexpr int VALUE_SOLDIER = 100;

	/** @brief 获取棋子基础价值 */
	int pieceValue(ChessLogic::PieceType type) const;

	/**
	 * @brief 评估函数
	 * @param logic 棋盘
	 * @return 分数（正 = 对 AI/黑方有利，负 = 对玩家/红方有利）
	 */
	int evaluate(const ChessLogic& logic) const;

	/**
	 * @brief 位置价值加成
	 * @param type 棋子类型
	 * @param side 所属方
	 * @param x, y 坐标
	 */
	int positionBonus(ChessLogic::PieceType type, ChessLogic::Side side, int x, int y) const;

	/**
	 * @brief Minimax + Alpha-Beta
	 * @param logic 棋盘（会被修改，调用方负责 undo）
	 * @param depth 剩余深度
	 * @param alpha
	 * @param beta
	 * @param side 当前走棋方
	 * @return 评估分
	 */
	int minimax(ChessLogic& logic, int depth, int alpha, int beta, ChessLogic::Side side) const;

	/**
	 * @brief 走法排序分（吃子价值高的排前面）
	 */
	int moveScore(const ChessLogic& logic, int fromX, int fromY, int toX, int toY) const;
};
