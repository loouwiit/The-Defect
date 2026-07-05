#pragma once

#include <cstdint>
#include <vector>
#include <utility>

/**
 * @brief 中国象棋核心逻辑
 *
 * 9×10 棋盘：[row 0-9][col 0-8]
 * row 0（顶）= 黑方（將）阵营
 * row 9（底）= 红方（帥）阵营
 *
 * 所有枚举/结构嵌套在类内，避免与项目中的 tetris 等模块冲突。
 */
class ChineseChessLogic
{
public:
    static constexpr int ROWS = 10;
    static constexpr int COLS = 9;

    // ── 嵌套类型（避免与 tetris 的 PieceType 冲突） ──
    enum class PieceType : uint8_t
    {
        None = 0,
        King,     // 帥 / 將
        Advisor,  // 仕 / 士
        Bishop,   // 相 / 象
        Knight,   // 馬
        Rook,     // 車
        Cannon,   // 炮 / 砲
        Pawn,     // 兵 / 卒
    };

    enum class Side : uint8_t
    {
        Red = 0,
        Black = 1,
    };

    struct Piece
    {
        PieceType type{ PieceType::None };
        Side side{ Side::Red };

        bool isEmpty() const { return type == PieceType::None; }
        bool operator==(const Piece& o) const { return type == o.type && side == o.side; }
        bool operator!=(const Piece& o) const { return !(*this == o); }
    };

    struct MoveRecord
    {
        int fromRow{}, fromCol{};
        int toRow{}, toCol{};
        Piece captured{};
    };

    ChineseChessLogic();

    /** @brief 重置到初始布局 */
    void reset();

    // ── 走棋 ──
    /** @brief 检查 from→to 是否合法（不含将帅对面检测，由 makeMove 统一处理） */
    bool isValidMove(int fromRow, int fromCol, int toRow, int toCol) const;

    /** @brief 执行走棋；成功返回 true 并记录到历史 */
    bool makeMove(int fromRow, int fromCol, int toRow, int toCol);

    /** @brief 悔棋；成功返回 true */
    bool undoLastMove();

    // ── 查询 ──
    const Piece& at(int row, int col) const { return m_board[row][col]; }
    Side currentTurn() const { return m_currentTurn; }
    int moveCount() const { return static_cast<int>(m_history.size()); }

    /** @brief 指定阵营是否被将军 */
    bool isInCheck(Side side) const;

    /** @brief 指定阵营是否被将杀 */
    bool isCheckmate(Side side) const;

    /** @brief 困毙 */
    bool isStalemate(Side side) const;

    /** @brief 获取某位置所有合法走法 */
    std::vector<std::pair<int, int>> getValidMoves(int row, int col) const;

    /** @brief 获取被吃掉的子（按阵营） */
    const std::vector<Piece>& capturedByRed() const { return m_capturedByRed; }
    const std::vector<Piece>& capturedByBlack() const { return m_capturedByBlack; }

    // ── 棋子中文名（8 项，索引对应 PieceType 枚举值） ──
    static constexpr const char* PIECE_NAMES_RED[8] = {
        "",       // None
        "帥",     // King
        "仕",     // Advisor
        "相",     // Bishop
        "馬",     // Knight
        "車",     // Rook
        "炮",     // Cannon
        "兵",     // Pawn
    };
    static constexpr const char* PIECE_NAMES_BLACK[8] = {
        "",       // None
        "將",     // King
        "士",     // Advisor
        "象",     // Bishop
        "馬",     // Knight
        "車",     // Rook
        "砲",     // Cannon
        "卒",     // Pawn
    };
    static const char* getPieceName(PieceType type, Side side);

private:
    Piece m_board[ROWS][COLS]{};

    Side m_currentTurn{Side::Red};
    std::vector<MoveRecord> m_history;

    std::vector<Piece> m_capturedByRed;   // 红方吃掉的子（被吃的是黑方）
    std::vector<Piece> m_capturedByBlack; // 黑方吃掉的子（被吃的是红方）

    // ── 布局辅助 ──
    void setPiece(int row, int col, PieceType type, Side side);
    void clearPiece(int row, int col);

    // ── 走法生成 ──
    bool inBoard(int r, int c) const;
    bool inPalace(int r, int c, Side side) const;
    bool inOwnHalf(int r, int c, Side side) const;

    /** @brief 生成原始走法（不检测被将军），结果写入 out */
    void generateRawMoves(int row, int col, std::vector<std::pair<int, int>>& out) const;

    // ── 检测 ──
    /** @brief 模拟走棋后是否仍被将军（用于合法性校验） */
    bool wouldBeInCheckAfterMove(int fromRow, int fromCol, int toRow, int toCol, Side side) const;

    /** @brief 将帅对面检测 */
    bool kingsAreFacing() const;
};
