#include "openingBook.hpp"
#include <algorithm>

// ============================================================
// 开局库数据
// ============================================================
//
// 走法编码：encodeMove(fromX, fromY, toX, toY)
// 坐标：x=0~8（左到右），y=0~9（上到下）
// 红方：y=5~9（下方），黑方：y=0~4（上方）
// 红方先走（奇数索引=红方，偶数索引=黑方）
//
// 开局线格式：moves[] 交替存储红方和黑方的走法
//=============================================================

const OpeningBook::OpeningLine OpeningBook::s_lines[] = {
	// ═════════════════════════════════════════════════════════
	// 1. 中炮对屏风马 (当头炮)
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 炮二平五 — 右炮移到中路 (7,7)→(4,7)
			encodeMove(7, 7, 4, 7),
			// B: 馬8進7 — 左马上 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 馬二進三 — 右马跳 (7,9)→(6,7)
			encodeMove(7, 9, 6, 7),
			// B: 馬2進3 — 右马上 (1,0)→(2,2)
			encodeMove(1, 0, 2, 2),
			// R: 車一平二 — 右车出 (8,9)→(7,9)
			encodeMove(8, 9, 7, 9),
			// B: 車9平8 — 左车出 (0,0)→(1,0)
			encodeMove(0, 0, 1, 0),
			// R: 車二進六 — 右车过河 (7,9)→(7,3)
			encodeMove(7, 9, 7, 3),
		},
		100, // 权重
	},

	// ═════════════════════════════════════════════════════════
	// 2. 中炮对屏风马 (另一变例: 黑方先跳左马)
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 炮二平五 (7,7)→(4,7)
			encodeMove(7, 7, 4, 7),
			// B: 馬2進3 (1,0)→(2,2)
			encodeMove(1, 0, 2, 2),
			// R: 馬二進三 (7,9)→(6,7)
			encodeMove(7, 9, 6, 7),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 車一平二 (8,9)→(7,9)
			encodeMove(8, 9, 7, 9),
			// B: 車9平8 (0,0)→(1,0)
			encodeMove(0, 0, 1, 0),
		},
		80,
	},

	// ═════════════════════════════════════════════════════════
	// 3. 中炮过河车对屏风马平炮兑车
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 炮二平五 (7,7)→(4,7)
			encodeMove(7, 7, 4, 7),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 馬二進三 (7,9)→(6,7)
			encodeMove(7, 9, 6, 7),
			// B: 馬2進3 (1,0)→(2,2)
			encodeMove(1, 0, 2, 2),
			// R: 車一平二 (8,9)→(7,9)
			encodeMove(8, 9, 7, 9),
			// B: 車9平8 (0,0)→(1,0)
			encodeMove(0, 0, 1, 0),
			// R: 車二進六 (7,9)→(7,3)
			encodeMove(7, 9, 7, 3),
			// B: 炮8平9 — 黑方平炮兑车 (7,2)→(1,2)
			encodeMove(7, 2, 1, 2),
			// R: 車二平三 (7,3)→(7,2) — 吃卒压马
			encodeMove(7, 3, 7, 2),
		},
		70,
	},

	// ═════════════════════════════════════════════════════════
	// 4. 飞相局 (相三進五)
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 相三進五 — 右相飞中 (6,9)→(4,7)
			encodeMove(6, 9, 4, 7),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 馬二進四 — 右马跳仕角 (7,9)→(5,7)
			encodeMove(7, 9, 5, 7),
			// B: 馬2進3 (1,0)→(2,2)
			encodeMove(1, 0, 2, 2),
			// R: 車一進一 — 右横车 (8,9)→(8,8)
			encodeMove(8, 9, 8, 8),
		},
		60,
	},

	// ═════════════════════════════════════════════════════════
	// 5. 仙人指路 (兵七進一)
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 兵七進一 — 七路兵前进 (6,6)→(6,5)
			encodeMove(6, 6, 6, 5),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 馬八進七 — 左马跳 (1,9)→(2,7)
			encodeMove(1, 9, 2, 7),
			// B: 車9平8 (0,0)→(1,0)
			encodeMove(0, 0, 1, 0),
		},
		50,
	},

	// ═════════════════════════════════════════════════════════
	// 6. 起马局 (馬二進三)
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 馬二進三 (7,9)→(6,7)
			encodeMove(7, 9, 6, 7),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 馬八進七 (1,9)→(2,7)
			encodeMove(1, 9, 2, 7),
			// B: 馬2進3 (1,0)→(2,2)
			encodeMove(1, 0, 2, 2),
			// R: 兵七進一 (6,6)→(6,5)
			encodeMove(6, 6, 6, 5),
		},
		50,
	},

	// ═════════════════════════════════════════════════════════
	// 7. 中炮对反宫马
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 炮二平五 (7,7)→(4,7)
			encodeMove(7, 7, 4, 7),
			// B: 馬2進3 (1,0)→(2,2)
			encodeMove(1, 0, 2, 2),
			// R: 馬二進三 (7,9)→(6,7)
			encodeMove(7, 9, 6, 7),
			// B: 炮8平6 — 士角炮 (7,2)→(5,2)
			encodeMove(7, 2, 5, 2),
			// R: 車一平二 (8,9)→(7,9)
			encodeMove(8, 9, 7, 9),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
		},
		40,
	},

	// ═════════════════════════════════════════════════════════
	// 8. 仕角炮开局
	// ═════════════════════════════════════════════════════════
	{
		{
			// R: 炮八平六 — 左炮平仕角 (1,7)→(5,7)
			encodeMove(1, 7, 5, 7),
			// B: 馬8進7 (7,0)→(6,2)
			encodeMove(7, 0, 6, 2),
			// R: 馬八進七 — 左马跳正 (1,9)→(2,7)
			encodeMove(1, 9, 2, 7),
		},
		30,
	},
};

// ============================================================
// 查询
// ============================================================

int OpeningBook::getBookMoves(const std::vector<ChessLogic::Move>& history,
							   std::vector<BookMove>& moves) const
{
	moves.clear();

	for (int li = 0; li < s_lineCount; li++)
	{
		const auto& line = s_lines[li];

		// 检查历史是否匹配此开局线
		int histMoves = static_cast<int>(history.size());
		if (histMoves >= 12) // 超过开局库范围
			continue;

		// 检查已走的走法是否匹配
		bool match = true;
		int matchedCount = 0;
		for (int i = 0; i < histMoves; i++)
		{
			auto enc = encodeMove(
				history[i].from.x, history[i].from.y,
				history[i].to.x, history[i].to.y);
			if (enc != line.moves[i])
			{
				match = false;
				break;
			}
			matchedCount++;
		}

		if (!match)
			continue;

		// 历史已匹配，检查是否有下一个走法
		int nextMoveIdx = histMoves;
		if (nextMoveIdx >= 12 || line.moves[nextMoveIdx] == 0)
			continue;

		// 提取下一个走法
		uint16_t enc = line.moves[nextMoveIdx];
		uint8_t toEnc = (enc >> 8) & 0xFF;
		uint8_t fromEnc = enc & 0xFF;

		// 检查是否已在列表中（避免重复）
		bool alreadyInList = false;
		for (const auto& m : moves)
		{
			if (m.fromEnc == fromEnc && m.toEnc == toEnc)
			{
				alreadyInList = true;
				break;
			}
		}

		if (!alreadyInList)
		{
			BookMove bm;
			bm.fromEnc = fromEnc;
			bm.toEnc = toEnc;
			bm.weight = line.weight;
			// 匹配的走法越多，权重越高（更倾向于已经深入的开局线）
			bm.weight = static_cast<uint16_t>(bm.weight + matchedCount * 10);
			moves.push_back(bm);
		}
	}

	// 按权重降序排列
	std::sort(moves.begin(), moves.end(),
		[](const BookMove& a, const BookMove& b) { return a.weight > b.weight; });

	return static_cast<int>(moves.size());
}
