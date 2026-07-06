#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "bleGamepad/gamepadState.hpp"
#include "chessLogic.hpp"
#include "chessAI.hpp"

/**
 * @brief 中国象棋游戏 App
 *
 * 支持：
 *   - 1P vs AI（简单随机走法）
 *   - 2P 本地同屏轮流操作
 *   - 触摸点击选子/走棋
 *   - 手柄摇杆焦点导航 + A/B 键
 *   - LVGL 预分配对象池渲染
 */
class ChineseChessApp : public App
{
public:
	static constexpr char TAG[] = "ChineseChess";

	/**
	 * @param display 显示器
	 * @param opponentType 0=AI, 1=Human 2P
	 */
	ChineseChessApp(Display* display, int opponentType);
	~ChineseChessApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	// ── 游戏核心 ──
	ChessLogic m_logic;
	int m_opponentType;   // 0=AI, 1=Human
	ChessAI m_ai;

	// ── 游戏循环线程 ──
	Thread m_thread;

	// ── 交互状态 ──
	bool m_selected = false;
	int m_selX = -1, m_selY = -1;
	std::vector<ChessLogic::Position> m_validMoves;

	// 光标位置（手柄导航，每玩家独立；P0=红方底线(4,7), P1=黑方底线(4,2)）
	int m_cursorX[MaxPlayers]{4, 4};
	int m_cursorY[MaxPlayers]{7, 2};

	// 输入去抖
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons[MaxPlayers]{};
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(400);

	// 脏标记（游戏循环中触发刷新）
	bool m_dirty = false;

	// ── 常量（适配 1280×720 横屏） ──
	static constexpr int CELL_SIZE = 64;
	static constexpr int BOARD_W = 1280;
	static constexpr int BOARD_H = 720;
	static constexpr int OFFSET_X = (BOARD_W - 8 * CELL_SIZE) / 2; // 384
	static constexpr int OFFSET_Y = 72;

	// ── LVGL 棋盘背景 ──
	lv_obj_t* m_boardContainer = nullptr;

	// 网格线
	lv_obj_t* m_gridLinesV[9]{};
	lv_obj_t* m_gridLinesH[10]{};
	lv_obj_t* m_riverLabel = nullptr;

	// 棋子对象池 [x][y]
	lv_obj_t* m_pieceBg[9][10]{};
	lv_obj_t* m_pieceText[9][10]{};

	// 选中高亮
	lv_obj_t* m_cursorHighlight[MaxPlayers]{};  // 手柄光标（每玩家）
	int m_prevSelX = -1, m_prevSelY = -1;      // 上一选中位置（恢复棋子样式用）

	// 合法走法标记
	lv_obj_t* m_moveMarkers[30]{};

	// ── UI 标签 ──
	lv_obj_t* m_turnLabel = nullptr;
	lv_obj_t* m_statusLabel = nullptr;
	lv_obj_t* m_gameOverLabel = nullptr;
	lv_obj_t* m_gameOverScoreLabel = nullptr;
	lv_obj_t* m_restartBtn = nullptr;
	lv_obj_t* m_backBtn = nullptr;

	// 焦点导航（GameOver 时）
	int8_t m_focusIdx = 0;

	// ── 视觉缓存（增量更新） ──
	struct CellVisual
	{
		ChessLogic::Side side = ChessLogic::Side::None;
		ChessLogic::PieceType type = ChessLogic::PieceType::None;
		bool alive = false;
	};
	CellVisual m_visualCache[9][10]{};

	// ── 方法 ──
	void createBoard(lv_obj_t* parent);
	void createPieces(lv_obj_t* parent);
	void createUI(lv_obj_t* parent);

	/** @brief 全量同步棋盘到 LVGL 对象 */
	void syncBoard();

	/** @brief 增量更新指定格 */
	void syncCell(int x, int y);

	/** @brief 更新选中/光标/标记 */
	void updateSelection();

	/** @brief 更新标签文本 */
	void updateLabels();

	/** @brief 显示/隐藏 GameOver UI */
	void showGameOver();
	void hideGameOver();

	/** @brief 游戏主循环（静态，C 风格回调） */
	static void gameLoop(void* param);

	// ── 输入处理 ──
	void onTouchClick(int px, int py);
	void handleSelect(uint8_t playerId);

	/** @brief 获取当前回合对应的玩家 ID */
	uint8_t getActivePlayerId() const;
	void handleCancel();

	// ── 焦点导航 ──
	void applyFocus();
	void activateFocus();

	// ── 辅助 ──
	int coordToGrid(int pixel, int offset, int maxGrid) const;
	int gridToCoord(int grid, int offset) const;

	// LVGL 回调
	static void btnRestartCb(lv_event_t* e);
	static void btnBackCb(lv_event_t* e);
	static void onScreenClickCb(lv_event_t* e);
};
