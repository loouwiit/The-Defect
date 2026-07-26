#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include "chessLogic.hpp"

/**
 * @brief 中国象棋开局库
 *
 * 存储常见开局走法序列，在 AI 搜索时优先考虑开局库中的走法。
 * 走法编码：fromX*10 + fromY → 低 8 位，toX*10 + toY → 高 8 位
 * 即 pack(fromX,fromY,toX,toY) = ((toX*10+toY) << 8) | (fromX*10+fromY)
 */
class OpeningBook
{
public:
	/** @brief 一个开局库走法（含权重） */
	struct BookMove
	{
		uint8_t fromEnc;  // fromX*10 + fromY
		uint8_t toEnc;    // toX*10 + toY
		uint16_t weight;  // 权重（越高越优先）
	};

	OpeningBook() = default;
	~OpeningBook() = default;

	/**
	 * @brief 根据当前走棋历史查询开局走法
	 * @param history 走棋历史（movePiece 产生的 Move 列表）
	 * @param[out] moves 输出的开局走法列表（已按 weight 降序排列）
	 * @return 找到的开局走法数
	 */
	int getBookMoves(const std::vector<ChessLogic::Move>& history,
					 std::vector<BookMove>& moves) const;

private:
	/** @brief 一条开局线：走法编码序列 */
	struct OpeningLine
	{
		uint16_t moves[12]; // 最多 12 个半走（6 回合）
		uint16_t weight;    // 该开局线的权重
	};

	static const OpeningLine s_lines[];
	static constexpr int s_lineCount = 8;

	/** @brief 编码一个走法 */
	static uint16_t encodeMove(int fromX, int fromY, int toX, int toY)
	{
		return static_cast<uint16_t>((toX * 10 + toY) << 8) | static_cast<uint16_t>(fromX * 10 + fromY);
	}
};
