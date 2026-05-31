#pragma once

#include <stdint.h>
#include <array>
#include <vector>

// ========== 测试模式 (设置为 1 启用测试) ==========
#define TEST_MODE 1

// ========== 常量 ==========

constexpr int BOARD_WIDTH  = 10;
constexpr int BOARD_HEIGHT = 22;    // 10×20 visible + 2 hidden rows
constexpr int VISIBLE_ROWS = 20;

constexpr int LOCK_DELAY_MS   = 500;
constexpr int DAS_DELAY_MS    = 170;
constexpr int ARR_INTERVAL_MS  = 50;
constexpr int SOFT_DROP_INTERVAL_MS = 50;
constexpr int GHOST_Y_OFFSET  = -1;  // ghost 位置为实际位置向上偏移一行

// ========== 枚举 ==========

enum class PieceType : uint8_t
{
	I = 0, O, T, S, Z, J, L,
	COUNT
};

enum class Rotation : uint8_t
{
	R = 0,  // 0°
	D = 1,  // 90°
	L = 2,  // 180°
	U = 3,  // 270°
	COUNT
};

enum class MoveType : uint8_t
{
	Left  = 0,
	Right = 1,
	Down  = 2,
	Drop  = 3,   // hard drop
};

enum class GamePhase : uint8_t
{
	Waiting,
	Running,
	Clearing,   // 行消除动画中
	GameOver,
};

// ========== SRS 形状数据 ==========

// 每种 Piece 在四种旋转状态下的标准形状 (相对于中心格)
// 形状: +[][][]+ (4×4)
struct PieceData
{
	int8_t minX;
	int8_t maxX;
	int8_t minY;
	int8_t maxY;
	int8_t blocks[4][4][4];  // [rot][y][x] = 1 if filled
};

extern const PieceData PIECE_DATA[7];

// SRS Wall Kick 数据 (相对偏移)
// [rot_from][rot_to][kick_index] = {dx, dy}
struct WallKickData
{
	int8_t dx, dy;

	constexpr WallKickData() : dx(0), dy(0) {}
	constexpr WallKickData(int8_t _dx, int8_t _dy) : dx(_dx), dy(_dy) {}
};

extern const WallKickData SRS_I[4][4][2];
extern const WallKickData SRS_STANDARD[4][4][2];

// ========== 前向声明 ==========
struct Board;

// ========== Piece ==========

struct Piece
{
	PieceType type;
	Rotation  rot;
	int       x, y;   // 中心格坐标，board 列/行

	Piece() : type(PieceType::I), rot(Rotation::R), x(0), y(0) {}
	Piece(PieceType t, Rotation r, int _x, int _y) : type(t), rot(r), x(_x), y(_y) {}

	// 获取形状 block 的绝对坐标
	void getBlocks(int8_t out[4][4]) const;

	// 返回当前旋转状态下的形状 (4×4 block 坐标)
	const PieceData& data() const { return PIECE_DATA[static_cast<uint8_t>(type)]; }

	// 是否与 board 碰撞 (输入为 board 坐标系的绝对坐标)
	bool collides(const Board& board) const;

	// 尝试旋转 (含 wall kick)
	bool tryRotate(const Board& board, Rotation newRot);

	// 尝试移动
	bool tryMove(const Board& board, int dx, int dy);
};

// ========== Board ==========

struct Board
{
	// 0 = empty, 1-7 = PieceType+1
	uint8_t cells[BOARD_HEIGHT][BOARD_WIDTH];

	Board() { clear(); }

	void clear();

	// 检查一行是否满
	bool isLineFull(int row) const;

	// 清除一行 (上面行下落)
	void clearLine(int row);

	// 放置 piece (不检查碰撞，假设已验证)
	void place(const Piece& piece);

	// 碰撞检测 (绝对坐标)
	bool check(int px, int py, const uint8_t shape[4][4]) const;

	// 获取已锁定格子的掩码 (用于渲染)
	uint32_t getColumnMask(int col) const;
};

// ========== 7-bag 随机生成器 ==========

class PieceQueue
{
public:
	PieceQueue();

	// 重置
	void reset();

	// 取下一个 piece
	PieceType next();

	// 查看下一个 (不弹出)
	PieceType peek() const;

	// 预览 k 个 piece
	void peekN(PieceType out[], int k) const;

private:
	std::array<PieceType, 7> bag_;  // 7-bag
	int index_;
};

// ========== Hold 系统 ==========

struct HoldState
{
	PieceType type = PieceType::COUNT;  // 无 held piece
	bool used = false;  // 本回合是否已使用 hold

	void reset() { type = PieceType::COUNT; used = false; }
	void hold(PieceType t) { type = t; }
};

// ========== 游戏状态 ==========

struct GameState
{
	Board        board;
	PieceQueue   queue;
	HoldState    hold;
	PieceType    currentPiece;
	PieceType    nextPiece;
	int          x, y;          // current piece 位置 (board col/row)
	Rotation     rot;           // current piece 旋转
	uint32_t     dropTimer;     // ms
	uint32_t     lockTimer;     // ms, -1 表示未触发 lock
	bool         isLocking;     // 是否在 locking 状态
	bool         usedHold;      // 本回合是否已 hold
	uint32_t     dasTimer;      // DAS
	MoveType     dasDirection;  // DAS 方向
	bool         dasActive;     // DAS 是否激活
	int          lines;         // 已消除行数
	int          score;         // 分数
	GamePhase    phase;
	int          clearAnimTimer; // 行消除动画计时
	uint8_t      clearLines[4];  // 待消除的行号
	int          clearLineCount;

	GameState();

	void reset();

	// 生成下一个 piece
	void spawnNext();

	// 玩家输入
	bool moveLeft(const Board& board);
	bool moveRight(const Board& board);
	bool moveDown(const Board& board);
	void hardDrop(const Board& board);
	bool rotateCW(const Board& board);
	bool rotateCCW(const Board& board);
	bool holdPiece(const Board& board);

	// 固定 piece 到 board
	void lock();

	// 检查游戏结束
	bool isGameOver() const;

	// 获取 ghost piece 位置
	int getGhostY() const;

	// 计算本次消行分数
	int calcScore(int linesCleared, bool isTSpin, bool isMiniTSpin) const;

	// 计算攻击行数 (发送给其他玩家)
	int calcAttackLines(int linesCleared, bool isTSpin) const;

	// 游戏 tick (每帧调用, dt_ms = 距上次调用的毫秒数)
	void tick(uint32_t dt_ms);

	// 软Drop速度 (ms/行)
	static constexpr uint32_t SOFT_DROP_INTERVAL = 50;

	// 重力速度 (ms/行, 初始值)
	static constexpr uint32_t GRAVITY_INTERVAL = 1000;

	// 是否启用软Drop
	bool softDropping = false;

	// 开始游戏
	void startGame();

	// 接受攻击行数 (从其他玩家)
	void receiveAttack(int lines);

	// 待消除的行号数组 (外部读取)
	uint8_t pendingClearLines[4];
	int pendingClearCount = 0;
};

// 测试模式
#if TEST_MODE
#if __cplusplus
extern "C" void tetris_run_tests(void);
#endif
#endif