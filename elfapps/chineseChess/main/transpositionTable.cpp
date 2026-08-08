#include "transpositionTable.hpp"
#include <cstring>
#include <algorithm>

TranspositionTable::TranspositionTable(size_t size)
{
	// 确保 size 是 2 的幂
	m_size = 1;
	while (m_size < size)
		m_size <<= 1;

	m_mask = m_size - 1;
	m_entries = new Entry[m_size]{};
}

TranspositionTable::~TranspositionTable()
{
	delete[] m_entries;
	m_entries = nullptr;
}

bool TranspositionTable::probe(uint64_t hash, int depth, int alpha, int beta,
							   int16_t& outScore, uint8_t& outBestFrom, uint8_t& outBestTo) const
{
	const auto& entry = m_entries[index(hash)];

	// hash 不匹配 → 未命中
	if (entry.hash != hash || entry.depth < 0)
		return false;

	// 保存最佳走法（即使 depth 不足也返回，供走法排序使用）
	outBestFrom = entry.bestFrom;
	outBestTo = entry.bestTo;

	// depth 不足 → 只能获取走法，不能获取评估
	if (entry.depth < depth)
		return false;

	// depth 足够 → 根据 flag 返回评估
	switch (entry.flag)
	{
	case EXACT:
		// 精确值，直接使用
		outScore = entry.score;
		return true;

	case LOWER_BOUND:
		// score >= beta 时可用（剪枝边界）
		if (entry.score >= beta)
		{
			outScore = entry.score;
			return true;
		}
		return false;

	case UPPER_BOUND:
		// score <= alpha 时可用（上界）
		if (entry.score <= alpha)
		{
			outScore = entry.score;
			return true;
		}
		return false;
	}

	return false;
}

void TranspositionTable::store(uint64_t hash, int depth, int16_t score, Flag flag,
							   uint8_t bestFrom, uint8_t bestTo)
{
	auto& entry = m_entries[index(hash)];

	// 替换策略：
	// 1. 空位直接存
	// 2. 相同 hash → 深度大的保留
	// 3. 不同 hash → 深度大的替换（始终保留搜索更深的条目）
	if (entry.hash == hash && entry.depth > depth)
	{
		// 已有条目深度更大且 hash 相同 → 不替换（保留更优质的信息）
		// 但更新最佳走法
		if (bestFrom != 0 || bestTo != 0)
		{
			entry.bestFrom = bestFrom;
			entry.bestTo = bestTo;
		}
		return;
	}

	// 新 hash 覆盖旧 hash，或深度更大
	if (entry.hash != hash && entry.depth > depth)
	{
		// 不同 hash 且旧条目深度更大 → 保留旧条目（深层搜索信息更珍贵）
		return;
	}

	entry.hash = hash;
	entry.depth = static_cast<int8_t>(depth);
	entry.score = score;
	entry.flag = static_cast<uint8_t>(flag);
	entry.bestFrom = bestFrom;
	entry.bestTo = bestTo;
}

void TranspositionTable::clear()
{
	for (size_t i = 0; i < m_size; i++)
		m_entries[i] = Entry{};
}

size_t TranspositionTable::usedCount() const
{
	size_t count = 0;
	for (size_t i = 0; i < m_size; i++)
	{
		if (m_entries[i].depth >= 0)
			count++;
	}
	return count;
}
