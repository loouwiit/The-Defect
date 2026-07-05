#include "chessLogic.hpp"
#include <cstring>
#include <algorithm>

// ============================================================
// 构造函数 / 初始布局
// ============================================================

ChessLogic::ChessLogic() = default;

void ChessLogic::placePiece(int x, int y, Side side, PieceType type)
{
	auto& p = m_board[x][y];
	p.side = side;
	p.type = type;
	p.alive = true;
}

void ChessLogic::reset()
{
	// 清空棋盘
	for (int x = 0; x < BOARD_W; x++)
		for (int y = 0; y < BOARD_H; y++)
			m_board[x][y] = {};

	// ── 黑方（上方, y=0~4） ──
	placePiece(0, 0, Side::Black, PieceType::Chariot);
	placePiece(1, 0, Side::Black, PieceType::Horse);
	placePiece(2, 0, Side::Black, PieceType::Elephant);
	placePiece(3, 0, Side::Black, PieceType::Advisor);
	placePiece(4, 0, Side::Black, PieceType::General);
	placePiece(5, 0, Side::Black, PieceType::Advisor);
	placePiece(6, 0, Side::Black, PieceType::Elephant);
	placePiece(7, 0, Side::Black, PieceType::Horse);
	placePiece(8, 0, Side::Black, PieceType::Chariot);
	placePiece(1, 2, Side::Black, PieceType::Cannon);
	placePiece(7, 2, Side::Black, PieceType::Cannon);
	for (int x = 0; x < 9; x += 2)
		placePiece(x, 3, Side::Black, PieceType::Soldier);

	// ── 红方（下方, y=5~9） ──
	placePiece(0, 9, Side::Red, PieceType::Chariot);
	placePiece(1, 9, Side::Red, PieceType::Horse);
	placePiece(2, 9, Side::Red, PieceType::Elephant);
	placePiece(3, 9, Side::Red, PieceType::Advisor);
	placePiece(4, 9, Side::Red, PieceType::General);
	placePiece(5, 9, Side::Red, PieceType::Advisor);
	placePiece(6, 9, Side::Red, PieceType::Elephant);
	placePiece(7, 9, Side::Red, PieceType::Horse);
	placePiece(8, 9, Side::Red, PieceType::Chariot);
	placePiece(1, 7, Side::Red, PieceType::Cannon);
	placePiece(7, 7, Side::Red, PieceType::Cannon);
	for (int x = 0; x < 9; x += 2)
		placePiece(x, 6, Side::Red, PieceType::Soldier);

	// 状态
	m_state = State::Playing;
	m_turn = Side::Red;
	m_winner = Side::None;
	m_history.clear();
	m_selected = false;
	m_validMoves.clear();
}

// ============================================================
// 查询
// ============================================================

const ChessLogic::Piece& ChessLogic::getPiece(int x, int y) const
{
	if (x < 0 || x >= BOARD_W || y < 0 || y >= BOARD_H)
	{
		static Piece none;
		return none;
	}
	return m_board[x][y];
}

// ============================================================
// 选中 & 走棋
// ============================================================

bool ChessLogic::selectPiece(int x, int y)
{
	if (m_state != State::Playing)
		return false;

	const auto& p = getPiece(x, y);
	if (p.isEmpty() || p.side != m_turn)
	{
		clearSelection();
		return false;
	}

	m_selected = true;
	m_selectedPos = { static_cast<int8_t>(x), static_cast<int8_t>(y) };
	m_validMoves = getValidMoves(x, y);
	return true;
}

void ChessLogic::clearSelection()
{
	m_selected = false;
	m_selectedPos = {};
	m_validMoves.clear();
}

bool ChessLogic::movePiece(int fromX, int fromY, int toX, int toY)
{
	if (m_state != State::Playing)
		return false;

	const auto& src = getPiece(fromX, fromY);
	if (src.isEmpty() || src.side != m_turn)
		return false;

	// 检查是否在合法走法列表中
	auto moves = getValidMoves(fromX, fromY);
	bool valid = false;
	for (const auto& m : moves)
	{
		if (m.x == toX && m.y == toY)
		{
			valid = true;
			break;
		}
	}
	if (!valid)
		return false;

	// 记录历史
	Move move;
	move.from = { static_cast<int8_t>(fromX), static_cast<int8_t>(fromY) };
	move.to = { static_cast<int8_t>(toX), static_cast<int8_t>(toY) };
	move.captured = m_board[toX][toY];
	m_history.push_back(move);

	// 执行走棋
	m_board[toX][toY] = m_board[fromX][fromY];
	m_board[fromX][fromY] = {};

	// 清空选中
	clearSelection();

	// 检查是否将死对方
	Side opponent = (m_turn == Side::Red) ? Side::Black : Side::Red;
	if (isCheckmate(opponent))
	{
		m_winner = m_turn;
		m_state = State::GameOver;
		return true;
	}

	// 切换回合
	m_turn = opponent;
	return true;
}

bool ChessLogic::undoMove()
{
	if (m_history.empty())
		return false;

	const auto& move = m_history.back();
	m_board[move.from.x][move.from.y] = m_board[move.to.x][move.to.y];
	m_board[move.to.x][move.to.y] = move.captured;

	m_history.pop_back();
	m_turn = (m_turn == Side::Red) ? Side::Black : Side::Red;
	m_winner = Side::None;
	m_state = State::Playing;
	clearSelection();
	return true;
}

// ============================================================
// 合法走法获取
// ============================================================

std::vector<ChessLogic::Position> ChessLogic::getValidMoves(int x, int y)
{
	std::vector<Position> result;
	const auto& piece = getPiece(x, y);
	if (piece.isEmpty())
		return result;

	for (int tx = 0; tx < BOARD_W; tx++)
	{
		for (int ty = 0; ty < BOARD_H; ty++)
		{
			if (tx == x && ty == y)
				continue;

			bool canReach = false;
			switch (piece.type)
			{
			case PieceType::General:  canReach = canMoveGeneral(x, y, tx, ty);  break;
			case PieceType::Advisor:  canReach = canMoveAdvisor(x, y, tx, ty);  break;
			case PieceType::Elephant: canReach = canMoveElephant(x, y, tx, ty); break;
			case PieceType::Horse:    canReach = canMoveHorse(x, y, tx, ty);    break;
			case PieceType::Chariot:  canReach = canMoveChariot(x, y, tx, ty);  break;
			case PieceType::Cannon:   canReach = canMoveCannon(x, y, tx, ty);   break;
			case PieceType::Soldier:  canReach = canMoveSoldier(x, y, tx, ty);  break;
			default: break;
			}

			if (canReach && !wouldBeInCheck(x, y, tx, ty, piece.side))
				result.push_back({ static_cast<int8_t>(tx), static_cast<int8_t>(ty) });
		}
	}

	return result;
}

// ============================================================
// 将军 / 将死 检测
// ============================================================

ChessLogic::Position ChessLogic::findGeneral(Side side) const
{
	for (int x = 0; x < BOARD_W; x++)
		for (int y = 0; y < BOARD_H; y++)
			if (m_board[x][y].type == PieceType::General && m_board[x][y].side == side && m_board[x][y].alive)
				return { static_cast<int8_t>(x), static_cast<int8_t>(y) };
	return { -1, -1 };
}

bool ChessLogic::isGeneralsFacing() const
{
	auto rPos = findGeneral(Side::Red);
	auto bPos = findGeneral(Side::Black);
	if (!rPos.valid() || !bPos.valid())
		return false;
	if (rPos.x != bPos.x)
		return false; // 不同列

	// 检查中间是否有棋子
	int minY = std::min(rPos.y, bPos.y);
	int maxY = std::max(rPos.y, bPos.y);
	for (int y = minY + 1; y < maxY; y++)
		if (!m_board[rPos.x][y].isEmpty())
			return false; // 有棋子挡住

	return true; // 面对面
}

bool ChessLogic::isInCheck(Side side) const
{
	auto generalPos = findGeneral(side);
	if (!generalPos.valid())
		return true; // 帅被吃，视为被将

	// 飞将规则
	if (isGeneralsFacing())
		return true;

	// 检查对方所有棋子是否能吃到将/帅
	Side attacker = (side == Side::Red) ? Side::Black : Side::Red;
	for (int x = 0; x < BOARD_W; x++)
	{
		for (int y = 0; y < BOARD_H; y++)
		{
			const auto& p = m_board[x][y];
			if (p.isEmpty() || p.side != attacker)
				continue;

			bool attacks = false;
			switch (p.type)
			{
			case PieceType::General:  attacks = canMoveGeneral(x, y, generalPos.x, generalPos.y);  break;
			case PieceType::Advisor:  attacks = canMoveAdvisor(x, y, generalPos.x, generalPos.y);  break;
			case PieceType::Elephant: attacks = canMoveElephant(x, y, generalPos.x, generalPos.y); break;
			case PieceType::Horse:    attacks = canMoveHorse(x, y, generalPos.x, generalPos.y);    break;
			case PieceType::Chariot:  attacks = canMoveChariot(x, y, generalPos.x, generalPos.y);  break;
			case PieceType::Cannon:   attacks = canMoveCannon(x, y, generalPos.x, generalPos.y);   break;
			case PieceType::Soldier:  attacks = canMoveSoldier(x, y, generalPos.x, generalPos.y);  break;
			default: break;
			}

			if (attacks)
				return true;
		}
	}

	return false;
}

bool ChessLogic::wouldBeInCheck(int fromX, int fromY, int toX, int toY, Side side)
{
	// 模拟走棋
	auto savedSrc = m_board[fromX][fromY];
	auto savedDst = m_board[toX][toY];
	m_board[toX][toY] = savedSrc;
	m_board[fromX][fromY] = {};

	bool inCheck = isInCheck(side);

	// 还原
	m_board[fromX][fromY] = savedSrc;
	m_board[toX][toY] = savedDst;

	return inCheck;
}

bool ChessLogic::isCheckmate(Side side)
{
	if (!isInCheck(side))
		return false;
	return !hasAnyLegalMove(side);
}

bool ChessLogic::hasAnyLegalMove(Side side)
{
	for (int x = 0; x < BOARD_W; x++)
	{
		for (int y = 0; y < BOARD_H; y++)
		{
			const auto& p = m_board[x][y];
			if (p.isEmpty() || p.side != side)
				continue;

			// 尝试所有可能目标
			for (int tx = 0; tx < BOARD_W; tx++)
			{
				for (int ty = 0; ty < BOARD_H; ty++)
				{
					if (tx == x && ty == y)
						continue;

					bool canReach = false;
					switch (p.type)
					{
					case PieceType::General:  canReach = canMoveGeneral(x, y, tx, ty);  break;
					case PieceType::Advisor:  canReach = canMoveAdvisor(x, y, tx, ty);  break;
					case PieceType::Elephant: canReach = canMoveElephant(x, y, tx, ty); break;
					case PieceType::Horse:    canReach = canMoveHorse(x, y, tx, ty);    break;
					case PieceType::Chariot:  canReach = canMoveChariot(x, y, tx, ty);  break;
					case PieceType::Cannon:   canReach = canMoveCannon(x, y, tx, ty);   break;
					case PieceType::Soldier:  canReach = canMoveSoldier(x, y, tx, ty);  break;
					default: break;
					}

					if (canReach && !wouldBeInCheck(x, y, tx, ty, side))
						return true;
				}
			}
		}
	}
	return false;
}

// ============================================================
// 辅助函数
// ============================================================

bool ChessLogic::inPalace(int x, int y, Side side)
{
	if (x < 3 || x > 5)
		return false;
	if (side == Side::Red)
		return y >= 7 && y <= 9;
	else
		return y >= 0 && y <= 2;
}

bool ChessLogic::onOwnHalf(int x, int y, Side side)
{
	if (side == Side::Red)
		return y >= 5;
	else
		return y <= 4;
}

bool ChessLogic::isFriendly(int x, int y, Side side) const
{
	const auto& p = getPiece(x, y);
	return !p.isEmpty() && p.side == side;
}

bool ChessLogic::canMoveTo(int x, int y, int tx, int ty, Side side) const
{
	if (tx < 0 || tx >= BOARD_W || ty < 0 || ty >= BOARD_H)
		return false;
	if (tx == x && ty == y)
		return false;
	if (isFriendly(tx, ty, side))
		return false;
	return true;
}

// ============================================================
// 各棋子走法
// ============================================================

bool ChessLogic::canMoveGeneral(int x, int y, int tx, int ty) const
{
	if (!canMoveTo(x, y, tx, ty, m_board[x][y].side))
		return false;
	if (!inPalace(tx, ty, m_board[x][y].side))
		return false;

	int dx = abs(tx - x);
	int dy = abs(ty - y);

	// 直走一格
	if ((dx == 1 && dy == 0) || (dx == 0 && dy == 1))
		return true;

	// 飞将：将可以直接吃对面的将（如果中间无子）
	if (m_board[tx][ty].type == PieceType::General)
	{
		// 检查是否同列且中间无子
		if (tx != x)
			return false;
		int minY = std::min(y, ty);
		int maxY = std::max(y, ty);
		for (int cy = minY + 1; cy < maxY; cy++)
			if (!m_board[x][cy].isEmpty())
				return false;
		return true;
	}

	return false;
}

bool ChessLogic::canMoveAdvisor(int x, int y, int tx, int ty) const
{
	if (!canMoveTo(x, y, tx, ty, m_board[x][y].side))
		return false;
	if (!inPalace(tx, ty, m_board[x][y].side))
		return false;

	int dx = abs(tx - x);
	int dy = abs(ty - y);

	// 斜走一格
	return (dx == 1 && dy == 1);
}

bool ChessLogic::canMoveElephant(int x, int y, int tx, int ty) const
{
	if (!canMoveTo(x, y, tx, ty, m_board[x][y].side))
		return false;

	// 象不能过河
	if (!onOwnHalf(tx, ty, m_board[x][y].side))
		return false;

	int dx = abs(tx - x);
	int dy = abs(ty - y);

	// 田字对角走
	if (dx != 2 || dy != 2)
		return false;

	// 塞象眼
	int blockX = (x + tx) / 2;
	int blockY = (y + ty) / 2;
	if (!m_board[blockX][blockY].isEmpty())
		return false;

	return true;
}

bool ChessLogic::canMoveHorse(int x, int y, int tx, int ty) const
{
	if (!canMoveTo(x, y, tx, ty, m_board[x][y].side))
		return false;

	int dx = tx - x;
	int dy = ty - y;
	int absDx = abs(dx);
	int absDy = abs(dy);

	// 日字走：先直走一格，再斜走一格
	int blockX = x, blockY = y;
	if (absDx == 2 && absDy == 1)
	{
		// 左右跳：蹩腿在中间直向格
		blockX = x + (dx > 0 ? 1 : -1);
	}
	else if (absDx == 1 && absDy == 2)
	{
		// 上下跳：蹩腿在中间直向格
		blockY = y + (dy > 0 ? 1 : -1);
	}
	else
	{
		return false;
	}

	// 蹩马腿
	if (!m_board[blockX][blockY].isEmpty())
		return false;

	return true;
}

bool ChessLogic::canMoveChariot(int x, int y, int tx, int ty) const
{
	if (!canMoveTo(x, y, tx, ty, m_board[x][y].side))
		return false;

	// 直线走
	if (tx != x && ty != y)
		return false;

	int stepX = (tx == x) ? 0 : ((tx > x) ? 1 : -1);
	int stepY = (ty == y) ? 0 : ((ty > y) ? 1 : -1);

	int cx = x + stepX;
	int cy = y + stepY;
	while (cx != tx || cy != ty)
	{
		if (!m_board[cx][cy].isEmpty())
			return false;
		cx += stepX;
		cy += stepY;
	}

	return true;
}

bool ChessLogic::canMoveCannon(int x, int y, int tx, int ty) const
{
	if (tx < 0 || tx >= BOARD_W || ty < 0 || ty >= BOARD_H)
		return false;
	if (tx == x && ty == y)
		return false;

	// 直线走
	if (tx != x && ty != y)
		return false;

	int stepX = (tx == x) ? 0 : ((tx > x) ? 1 : -1);
	int stepY = (ty == y) ? 0 : ((ty > y) ? 1 : -1);

	int jumpCount = 0;
	int cx = x + stepX;
	int cy = y + stepY;
	while (cx != tx || cy != ty)
	{
		if (!m_board[cx][cy].isEmpty())
			jumpCount++;
		cx += stepX;
		cy += stepY;
	}

	const auto& target = getPiece(tx, ty);

	if (target.isEmpty())
	{
		// 不吃子：不能越子
		return jumpCount == 0;
	}
	else
	{
		// 吃子：必须且仅能跳过一子（炮架）
		if (isFriendly(tx, ty, m_board[x][y].side))
			return false; // 不能吃己方
		return jumpCount == 1;
	}
}

bool ChessLogic::canMoveSoldier(int x, int y, int tx, int ty) const
{
	if (!canMoveTo(x, y, tx, ty, m_board[x][y].side))
		return false;

	int dx = tx - x;
	int dy = ty - y;
	int absDx = abs(dx);
	int absDy = abs(dy);

	Side side = m_board[x][y].side;

	if (onOwnHalf(x, y, side))
	{
		// 过河前：只能向前走一格
		if (absDx != 0 || absDy != 1)
			return false;
		if (side == Side::Red && dy >= 0)   // 红方：y 减小为向前（上）
			return false;
		if (side == Side::Black && dy <= 0)  // 黑方：y 増大为向前（下）
			return false;
		return true;
	}
	else
	{
		// 过河后：可以向前或左右走一格
		if (absDx + absDy != 1)
			return false;
		if (side == Side::Red && dy > 0)    // 红方不能后退
			return false;
		if (side == Side::Black && dy < 0)  // 黑方不能后退
			return false;
		return true;
	}
}

// ============================================================
// 棋子文字
// ============================================================

const char* ChessLogic::pieceChar(PieceType type, Side side)
{
	switch (type)
	{
	case PieceType::General:  return (side == Side::Red) ? "帅" : "将";
	case PieceType::Advisor:  return (side == Side::Red) ? "仕" : "士";
	case PieceType::Elephant: return (side == Side::Red) ? "相" : "象";
	case PieceType::Horse:    return (side == Side::Red) ? "馬" : "馬";
	case PieceType::Chariot:  return (side == Side::Red) ? "車" : "車";
	case PieceType::Cannon:   return (side == Side::Red) ? "炮" : "砲";
	case PieceType::Soldier:  return (side == Side::Red) ? "兵" : "卒";
	default:                  return "";
	}
}
