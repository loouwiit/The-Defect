#pragma once

#include <cstdint>
#include <vector>
#include <array>

/**
 * @brief 中国象棋核心逻辑（纯数据，无 LVGL 依赖）
 *
 * 棋盘坐标：
 *   x: 0~8（左到右）
 *   y: 0~9（上到下）
 *   y=0~4 = 黑方（上方），y=5~9 = 红方（下方）
 *   红方先走
 */
class ChessLogic
{
public:
	// ── 棋子颜色 ──
	enum class Side : uint8_t
	{
		None,
		Red,
		Black,
	};

	// ── 棋子类型 ──
	enum class PieceType : uint8_t
	{
		None,
		General,   // 将/帅
		Advisor,   // 士/仕
		Elephant,  // 象/相
		Horse,     // 馬
		Chariot,   // 車
		Cannon,    // 砲/炮
		Soldier,   // 卒/兵
	};

	// ── 棋子 ──
	struct Piece
	{
		Side side = Side::None;
		PieceType type = PieceType::None;
		bool alive = false;

		bool isEmpty() const { return type == PieceType::None || !alive; }
	};

	// ── 坐标 ──
	struct Position
	{
		int8_t x = -1, y = -1;

		bool valid() const { return x >= 0 && x <= 8 && y >= 0 && y <= 9; }
		bool operator==(const Position& o) const { return x == o.x && y == o.y; }
		bool operator!=(const Position& o) const { return x != o.x || y != o.y; }
	};

	// ── 走法 ──
	struct Move
	{
		Position from;
		Position to;
		Piece captured;  // 被吃掉的棋子
	};

	// ── 游戏状态 ──
	enum class State : uint8_t
	{
		Waiting,   // 等待开始
		Playing,   // 游戏中
		Paused,    // 暂停
		GameOver,  // 游戏结束
	};

	ChessLogic();
	~ChessLogic() = default;

	// ═════════════════════════════════════════════════════════
	// 游戏控制
	// ═════════════════════════════════════════════════════════

	/** @brief 重置棋盘到初始布局 */
	void reset();

	/** @brief 选择某位置的棋子，返回 true 表示选中 */
	bool selectPiece(int x, int y);

	/** @brief 走棋，返回 true 表示合法走法已执行 */
	bool movePiece(int fromX, int fromY, int toX, int toY);

	/** @brief 取消选中 */
	void clearSelection();

	/** @brief 悔棋 */
	bool undoMove();

	// ═════════════════════════════════════════════════════════
	// 查询
	// ═════════════════════════════════════════════════════════

	const Piece& getPiece(int x, int y) const;
	const Piece& getPiece(Position p) const { return getPiece(p.x, p.y); }

	/** @brief 获取某棋子的合法走法（返回坐标列表） */
	std::vector<Position> getValidMoves(int x, int y);

	/** @brief 当前走法的目标位置列表（selectPiece 后调用） */
	const std::vector<Position>& getCurrentValidMoves() const { return m_validMoves; }

	/** @brief 判断某方是否被将军 */
	bool isInCheck(Side side) const;

	/** @brief 判断某方是否被将死 */
	bool isCheckmate(Side side);

	/** @brief 当前回合方 */
	Side getCurrentTurn() const { return m_turn; }

	/** @brief 获取当前选中位置 */
	Position getSelectedPos() const { return m_selectedPos; }

	/** @brief 是否有选中 */
	bool hasSelection() const { return m_selected; }

	/** @brief 游戏状态 */
	State getState() const { return m_state; }
	void setState(State s) { m_state = s; }

	/** @brief 胜利方 */
	Side getWinner() const { return m_winner; }

	/** @brief 走棋历史 */
	const std::vector<Move>& getHistory() const { return m_history; }

	/** @brief 获取棋子类型的中文字符 */
	static const char* pieceChar(PieceType type, Side side);

	/** @brief 是否在己方半场 */
	static bool onOwnHalf(int x, int y, Side side);

	/** @brief 棋盘宽度/高度常量 */
	static constexpr int BOARD_W = 9;
	static constexpr int BOARD_H = 10;

private:
	// ── 棋盘数据 ──
	// m_board[x][y]，[0][0] 为左上角
	std::array<std::array<Piece, BOARD_H>, BOARD_W> m_board{};

	State m_state = State::Waiting;
	Side m_turn = Side::Red;
	Side m_winner = Side::None;
	std::vector<Move> m_history;

	// 选中状态
	bool m_selected = false;
	Position m_selectedPos{};
	std::vector<Position> m_validMoves;

	// ═════════════════════════════════════════════════════════
	// 初始化辅助
	// ═════════════════════════════════════════════════════════
	void placePiece(int x, int y, Side side, PieceType type);

	// ═════════════════════════════════════════════════════════
	// 走法生成（检查目标格是否可达，不含将军检测）
	// ═════════════════════════════════════════════════════════

	bool canMoveGeneral(int x, int y, int tx, int ty) const;
	bool canMoveAdvisor(int x, int y, int tx, int ty) const;
	bool canMoveElephant(int x, int y, int tx, int ty) const;
	bool canMoveHorse(int x, int y, int tx, int ty) const;
	bool canMoveChariot(int x, int y, int tx, int ty) const;
	bool canMoveCannon(int x, int y, int tx, int ty) const;
	bool canMoveSoldier(int x, int y, int tx, int ty) const;

	/** @brief 通⽤⾛法检查：目标格是⾃⼰的⼦则不合法 */
	bool canMoveTo(int x, int y, int tx, int ty, Side side) const;

	// ═════════════════════════════════════════════════════════
	// 辅助
	// ═════════════════════════════════════════════════════════

	/** @brief 是否在九宫（将/⼠活动范围） */
	static bool inPalace(int x, int y, Side side);

	/** @brief (tx,ty) 是否有己方的子 */
	bool isFriendly(int x, int y, Side side) const;

	/** @brief 两个将是否面对面（飞将规则） */
	bool isGeneralsFacing() const;

	/** @brief 模拟⾛棋后是否处于被将军状态 */
	bool wouldBeInCheck(int fromX, int fromY, int toX, int toY, Side side);

	/** @brief 获取某⽅将/帅的位置 */
	Position findGeneral(Side side) const;

	/** @brief 某⼀⽅是否有合法⾛法 */
	bool hasAnyLegalMove(Side side);
};
