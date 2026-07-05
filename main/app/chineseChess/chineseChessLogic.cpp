#include "chineseChessLogic.hpp"
#include <cstring>
#include <algorithm>

static_assert(static_cast<int>(ChineseChessLogic::PieceType::Pawn) == 7,
    "PIECE_NAMES arrays must match PieceType enum");

// ════════════════════════════════════════════════════════════════
// 工具
// ════════════════════════════════════════════════════════════════

const char* ChineseChessLogic::getPieceName(PieceType type, Side side)
{
    if (type == PieceType::None) return "";
    auto idx = static_cast<int>(type);
    return (side == Side::Red) ? PIECE_NAMES_RED[idx] : PIECE_NAMES_BLACK[idx];
}

// ════════════════════════════════════════════════════════════════
// 构造 / 重置
// ════════════════════════════════════════════════════════════════

ChineseChessLogic::ChineseChessLogic()
{
    reset();
}

void ChineseChessLogic::setPiece(int row, int col, PieceType type, Side side)
{
    m_board[row][col] = { type, side };
}

void ChineseChessLogic::clearPiece(int row, int col)
{
    m_board[row][col] = {};
}

void ChineseChessLogic::reset()
{
    // 清空
    for (auto& row : m_board)
        for (auto& cell : row)
            cell = {};

    // ── 黑方（顶，row 0）──
    setPiece(0, 0, PieceType::Rook, Side::Black);
    setPiece(0, 1, PieceType::Knight, Side::Black);
    setPiece(0, 2, PieceType::Bishop, Side::Black);
    setPiece(0, 3, PieceType::Advisor, Side::Black);
    setPiece(0, 4, PieceType::King, Side::Black);
    setPiece(0, 5, PieceType::Advisor, Side::Black);
    setPiece(0, 6, PieceType::Bishop, Side::Black);
    setPiece(0, 7, PieceType::Knight, Side::Black);
    setPiece(0, 8, PieceType::Rook, Side::Black);
    setPiece(2, 1, PieceType::Cannon, Side::Black);
    setPiece(2, 7, PieceType::Cannon, Side::Black);
    setPiece(3, 0, PieceType::Pawn, Side::Black);
    setPiece(3, 2, PieceType::Pawn, Side::Black);
    setPiece(3, 4, PieceType::Pawn, Side::Black);
    setPiece(3, 6, PieceType::Pawn, Side::Black);
    setPiece(3, 8, PieceType::Pawn, Side::Black);

    // ── 红方（底，row 9）──
    setPiece(9, 0, PieceType::Rook, Side::Red);
    setPiece(9, 1, PieceType::Knight, Side::Red);
    setPiece(9, 2, PieceType::Bishop, Side::Red);
    setPiece(9, 3, PieceType::Advisor, Side::Red);
    setPiece(9, 4, PieceType::King, Side::Red);
    setPiece(9, 5, PieceType::Advisor, Side::Red);
    setPiece(9, 6, PieceType::Bishop, Side::Red);
    setPiece(9, 7, PieceType::Knight, Side::Red);
    setPiece(9, 8, PieceType::Rook, Side::Red);
    setPiece(7, 1, PieceType::Cannon, Side::Red);
    setPiece(7, 7, PieceType::Cannon, Side::Red);
    setPiece(6, 0, PieceType::Pawn, Side::Red);
    setPiece(6, 2, PieceType::Pawn, Side::Red);
    setPiece(6, 4, PieceType::Pawn, Side::Red);
    setPiece(6, 6, PieceType::Pawn, Side::Red);
    setPiece(6, 8, PieceType::Pawn, Side::Red);

    m_currentTurn = Side::Red;
    m_history.clear();
    m_capturedByRed.clear();
    m_capturedByBlack.clear();
}

// ════════════════════════════════════════════════════════════════
// 落子范围检测
// ════════════════════════════════════════════════════════════════

bool ChineseChessLogic::inBoard(int r, int c) const
{
    return r >= 0 && r < ROWS && c >= 0 && c < COLS;
}

bool ChineseChessLogic::inPalace(int r, int c, Side side) const
{
    if (c < 3 || c > 5) return false;
    if (side == Side::Red) return r >= 7 && r <= 9;
    else                  return r >= 0 && r <= 2;
}

bool ChineseChessLogic::inOwnHalf(int r, int c, Side side) const
{
    if (side == Side::Red) return r >= 5;
    else                  return r <= 4;
}

// ════════════════════════════════════════════════════════════════
// 原始走法生成（不检测被将军）
// ════════════════════════════════════════════════════════════════

void ChineseChessLogic::generateRawMoves(int row, int col,
    std::vector<std::pair<int, int>>& out) const
{
    const Piece& piece = m_board[row][col];
    if (piece.isEmpty()) return;

    auto side = piece.side;
    int r, c;

    auto addIf = [&](int tr, int tc) {
        if (!inBoard(tr, tc)) return;
        const auto& target = m_board[tr][tc];
        if (target.isEmpty() || target.side != side)
            out.emplace_back(tr, tc);
    };

    switch (piece.type)
    {
    case PieceType::King:
    {
        // 上下左右各 1 步，限九宫
        int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& d : dirs)
        {
            r = row + d[0]; c = col + d[1];
            if (inPalace(r, c, side))
                addIf(r, c);
        }
        break;
    }

    case PieceType::Advisor:
    {
        // 斜走 1 步，限九宫
        int dirs[4][2] = { {1,1},{1,-1},{-1,1},{-1,-1} };
        for (auto& d : dirs)
        {
            r = row + d[0]; c = col + d[1];
            if (inPalace(r, c, side))
                addIf(r, c);
        }
        break;
    }

    case PieceType::Bishop:
    {
        // 田字对角，不能被塞象眼
        int dirs[4][2] = { {2,2},{2,-2},{-2,2},{-2,-2} };
        for (auto& d : dirs)
        {
            r = row + d[0]; c = col + d[1];
            int er = row + d[0] / 2;
            int ec = col + d[1] / 2;
            if (inBoard(r, c) && inOwnHalf(r, c, side)
                && m_board[er][ec].isEmpty())
                addIf(r, c);
        }
        break;
    }

    case PieceType::Knight:
    {
        // 日字，不能被蹩马腿
        static const struct { int dr, dc, er, ec; } legs[8] = {
            { 2, 1, 1, 0 }, { 2,-1, 1, 0 },
            {-2, 1,-1, 0 }, {-2,-1,-1, 0 },
            { 1, 2, 0, 1 }, { 1,-2, 0,-1 },
            {-1, 2, 0, 1 }, {-1,-2, 0,-1 },
        };
        for (auto& l : legs)
        {
            r = row + l.dr; c = col + l.dc;
            if (inBoard(r, c) && m_board[row + l.er][col + l.ec].isEmpty())
                addIf(r, c);
        }
        break;
    }

    case PieceType::Rook:
    {
        // 直线
        int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& d : dirs)
        {
            r = row + d[0]; c = col + d[1];
            while (inBoard(r, c))
            {
                const auto& t = m_board[r][c];
                if (t.isEmpty())
                {
                    out.emplace_back(r, c);
                }
                else
                {
                    if (t.side != side)
                        out.emplace_back(r, c);
                    break;
                }
                r += d[0]; c += d[1];
            }
        }
        break;
    }

    case PieceType::Cannon:
    {
        // 直线移动 + 隔一子吃
        int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& d : dirs)
        {
            r = row + d[0]; c = col + d[1];
            bool foundScreen = false;
            while (inBoard(r, c))
            {
                const auto& t = m_board[r][c];
                if (!foundScreen)
                {
                    if (t.isEmpty())
                    {
                        out.emplace_back(r, c);
                    }
                    else
                    {
                        foundScreen = true; // 炮架
                    }
                }
                else
                {
                    if (!t.isEmpty())
                    {
                        if (t.side != side)
                            out.emplace_back(r, c);
                        break; // 炮架后的第一个子即是目标或障碍
                    }
                }
                r += d[0]; c += d[1];
            }
        }
        break;
    }

    case PieceType::Pawn:
    {
        int forward = (side == Side::Red) ? -1 : 1;
        // 前进一步
        r = row + forward; c = col;
        if (inBoard(r, c))
            addIf(r, c);
        // 过河后可横走
        if (!inOwnHalf(row, col, side))
        {
            for (int dc : { -1, 1 })
            {
                r = row; c = col + dc;
                if (inBoard(r, c))
                    addIf(r, c);
            }
        }
        break;
    }

    default:
        break;
    }
}

// ════════════════════════════════════════════════════════════════
// 将军 / 对面 / 合法性
// ════════════════════════════════════════════════════════════════

bool ChineseChessLogic::kingsAreFacing() const
{
    // 找到两个将/帥
    int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            if (m_board[r][c].type == PieceType::King)
            {
                if (r1 < 0) { r1 = r; c1 = c; }
                else { r2 = r; c2 = c; }
            }
    if (r2 < 0) return false;

    // 不同列则不为对面
    if (c1 != c2) return false;

    // 检查中间是否有棋子阻挡
    int minR = (r1 < r2) ? r1 : r2;
    int maxR = (r1 > r2) ? r1 : r2;
    for (int r = minR + 1; r < maxR; ++r)
        if (!m_board[r][c1].isEmpty())
            return false;  // 有棋子阻挡，不算对面

    return true;  // 同列且中间无子，将帅对面
}

bool ChineseChessLogic::isInCheck(Side side) const
{
    // 找到该方将/帥位置
    int kingRow = -1, kingCol = -1;
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            if (m_board[r][c].type == PieceType::King && m_board[r][c].side == side)
            {
                kingRow = r; kingCol = c;
                break;
            }
    if (kingRow < 0) return false;

    // 遍历对方所有子，看是否能吃到这个将/帥
    Side enemy = (side == Side::Red) ? Side::Black : Side::Red;
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
        {
            const auto& p = m_board[r][c];
            if (p.isEmpty() || p.side != enemy) continue;

            std::vector<std::pair<int, int>> moves;
            generateRawMoves(r, c, moves);
            for (auto& m : moves)
                if (m.first == kingRow && m.second == kingCol)
                    return true;
        }
    return false;
}

bool ChineseChessLogic::wouldBeInCheckAfterMove(int fromRow, int fromCol,
    int toRow, int toCol, Side side) const
{
    // 拷贝棋盘模拟走棋，避免修改真实状态
    ChineseChessLogic copy = *this;
    copy.m_board[toRow][toCol] = copy.m_board[fromRow][fromCol];
    copy.m_board[fromRow][fromCol] = {};

    // 检查是否将帅对面
    if (copy.kingsAreFacing()) return true;

    return copy.isInCheck(side);
}

// ════════════════════════════════════════════════════════════════
// 公开接口
// ════════════════════════════════════════════════════════════════

bool ChineseChessLogic::isValidMove(int fromRow, int fromCol, int toRow, int toCol) const
{
    if (!inBoard(fromRow, fromCol) || !inBoard(toRow, toCol))
        return false;
    const Piece& piece = m_board[fromRow][fromCol];
    if (piece.isEmpty()) return false;
    if (piece.side != m_currentTurn) return false;
    if (fromRow == toRow && fromCol == toCol) return false;

    // 先生成原始走法，看 to 是否在其中
    std::vector<std::pair<int, int>> raw;
    generateRawMoves(fromRow, fromCol, raw);
    bool inRaw = false;
    for (auto& m : raw)
        if (m.first == toRow && m.second == toCol)
        {
            inRaw = true;
            break;
        }
    if (!inRaw) return false;

    // 检测走后是否被将军（含将帅对面）
    return !wouldBeInCheckAfterMove(fromRow, fromCol, toRow, toCol, piece.side);
}

bool ChineseChessLogic::makeMove(int fromRow, int fromCol, int toRow, int toCol)
{
    if (!isValidMove(fromRow, fromCol, toRow, toCol))
        return false;

    Piece captured = m_board[toRow][toCol];

    // 记录
    m_history.push_back({ fromRow, fromCol, toRow, toCol, captured });

    // 执行
    m_board[toRow][toCol] = m_board[fromRow][fromCol];
    m_board[fromRow][fromCol] = {};

    // 记录被吃的子
    if (!captured.isEmpty())
    {
        if (m_currentTurn == Side::Red)
            m_capturedByRed.push_back(captured);
        else
            m_capturedByBlack.push_back(captured);
    }

    // 换手
    m_currentTurn = (m_currentTurn == Side::Red) ? Side::Black : Side::Red;
    return true;
}

bool ChineseChessLogic::undoLastMove()
{
    if (m_history.empty()) return false;

    const auto& last = m_history.back();

    // 还原
    m_board[last.fromRow][last.fromCol] = m_board[last.toRow][last.toCol];
    m_board[last.toRow][last.toCol] = last.captured;

    // 移除被吃记录
    Side prevTurn = (m_currentTurn == Side::Red) ? Side::Black : Side::Red;
    if (!last.captured.isEmpty())
    {
        if (prevTurn == Side::Red)
        {
            if (!m_capturedByRed.empty())
                m_capturedByRed.pop_back();
        }
        else
        {
            if (!m_capturedByBlack.empty())
                m_capturedByBlack.pop_back();
        }
    }

    m_history.pop_back();
    m_currentTurn = prevTurn;
    return true;
}

std::vector<std::pair<int, int>> ChineseChessLogic::getValidMoves(int row, int col) const
{
    std::vector<std::pair<int, int>> result;
    if (!inBoard(row, col)) return result;
    const Piece& piece = m_board[row][col];
    if (piece.isEmpty() || piece.side != m_currentTurn) return result;

    std::vector<std::pair<int, int>> raw;
    generateRawMoves(row, col, raw);

    for (auto& m : raw)
        if (!wouldBeInCheckAfterMove(row, col, m.first, m.second, piece.side))
            result.push_back(m);

    return result;
}

bool ChineseChessLogic::isCheckmate(Side side) const
{
    if (!isInCheck(side)) return false;

    // 遍历该方所有子，看是否有合法走法
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
        {
            const auto& p = m_board[r][c];
            if (p.isEmpty() || p.side != side) continue;

            auto valid = getValidMoves(r, c);
            if (!valid.empty()) return false;
        }
    return true;
}

bool ChineseChessLogic::isStalemate(Side side) const
{
    if (isInCheck(side)) return false;

    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
        {
            const auto& p = m_board[r][c];
            if (p.isEmpty() || p.side != side) continue;

            auto valid = getValidMoves(r, c);
            if (!valid.empty()) return false;
        }
    return true;
}
