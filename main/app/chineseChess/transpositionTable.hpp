#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief 置换表（Transposition Table）
 *
 * 用于缓存已搜索过的局面评估结果，避免重复搜索。
 * 使用 Zobrist Hashing 的 64 位 hash 作为键。
 *
 * 条目：
 *   - hash: 棋盘的 Zobrist hash（64位，低 18 位作为索引）
 *   - score: 评估分
 *   - depth: 搜索深度
 *   - flag: 节点类型（EXACT / LOWER_BOUND / UPPER_BOUND）
 *   - bestMove: 该局面的最佳走法（用于走法排序）
 *
 * 总大小：2^18 = 262K 条目 × 20 字节 ≈ 5.2MB
 */
class TranspositionTable
{
public:
	/** @brief 节点（搜索）类型 */
	enum Flag : uint8_t
	{
		EXACT,        // 精确值（alpha < score < beta）
		LOWER_BOUND,  // 下界（剪枝，score >= beta）
		UPPER_BOUND,  // 上界（score <= alpha）
	};

	/** @brief 置换表条目 */
	struct Entry
	{
		uint64_t hash = 0;        // Zobrist hash（全量校验）
		int16_t  score = 0;       // 评估分（-32768~32767）
		int8_t   depth = -1;      // 搜索深度（-1 = 空条目）
		uint8_t  flag = EXACT;    // 节点类型
		// 最佳走法（编码为紧凑格式）
		uint8_t  bestFrom = 0;    // fromX * 10 + fromY
		uint8_t  bestTo = 0;      // toX * 10 + toY
	};
	static_assert(sizeof(Entry) == 16, "TranspositionTable::Entry 应为 16 字节");

	/**
	 * @brief 构造置换表
	 * @param size 条目数量（2 的幂），默认 2^18 = 262144
	 */
	TranspositionTable(size_t size = 1 << 18);
	~TranspositionTable();

	// 禁止拷贝
	TranspositionTable(const TranspositionTable&) = delete;
	TranspositionTable& operator=(const TranspositionTable&) = delete;

	/**
	 * @brief 探测置换表
	 * @param hash 棋盘 hash
	 * @param depth 当前搜索深度
	 * @param alpha
	 * @param beta
	 * @param[out] bestMoveFrom 编码的最佳走法来源（0~89，调用方解码）
	 * @param[out] bestMoveTo 编码的最佳走法目标（0~89）
	 * @return true=命中，score 有效；false=未命中
	 */
	bool probe(uint64_t hash, int depth, int alpha, int beta,
			   int16_t& outScore, uint8_t& outBestFrom, uint8_t& outBestTo) const;

	/**
	 * @brief 存储条目到置换表
	 * @param hash 棋盘 hash
	 * @param depth 搜索深度
	 * @param score 评估分
	 * @param flag 节点类型
	 * @param bestFrom 最佳走法来源（编码）
	 * @param bestTo 最佳走法目标（编码）
	 */
	void store(uint64_t hash, int depth, int16_t score, Flag flag,
			   uint8_t bestFrom, uint8_t bestTo);

	/** @brief 清空置换表 */
	void clear();

	/** @brief 获取置换表大小 */
	size_t size() const { return m_size; }

	/** @brief 获取已使用条目数 */
	size_t usedCount() const;

	/** @brief 获取 hash 掩码 */
	size_t mask() const { return m_mask; }

private:
	Entry*   m_entries = nullptr;
	size_t   m_size = 0;
	size_t   m_mask = 0;

	/** @brief 从 hash 计算索引 */
	size_t index(uint64_t hash) const { return hash & m_mask; }
};
