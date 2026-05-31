#include "tetris.hpp"
#include "esp_log.h"
#include <cstring>

static constexpr char TAG[] = "Tetris";

// ========== 测试模式 (默认关闭) ==========
// 如需启用测试, 在 tetris.hpp 中设置 TEST_MODE=1

#if TEST_MODE
#include "esp_random.h"

static void test_piece_rotation()
{
	ESP_LOGI(TAG, "=== Test Piece Rotation ===");
	Board board;
	Piece p(PieceType::T, Rotation::R, 5, 10);

	// 测试 R -> D 旋转
	Piece p1 = p;
	bool ok = p1.tryRotate(board, Rotation::D);
	ESP_LOGI(TAG, "T R->D rotatable=%d pos=(%d,%d)", ok, p1.x, p1.y);

	// 测试 R -> L 旋转 (180)
	Piece p2 = p;
	ok = p2.tryRotate(board, Rotation::L);
	ESP_LOGI(TAG, "T R->L rotatable=%d pos=(%d,%d)", ok, p2.x, p2.y);

	// 测试 I 旋转
	Piece iPiece(PieceType::I, Rotation::R, 5, 10);
	Piece i1 = iPiece;
	ok = i1.tryRotate(board, Rotation::D);
	ESP_LOGI(TAG, "I R->D rotatable=%d pos=(%d,%d)", ok, i1.x, i1.y);

	ESP_LOGI(TAG, "Piece rotation test PASSED");
}

static void test_board_collision()
{
	ESP_LOGI(TAG, "=== Test Board Collision ===");
	Board board;

	// 放置一个T方块在顶部
	Piece p(PieceType::T, Rotation::R, 5, 1);
	board.place(p);

	// 测试碰撞
	Piece test(PieceType::I, Rotation::R, 5, 1);
	bool collides = test.collides(board);
	ESP_LOGI(TAG, "I at (5,1) collides with board=%d (expected 1)", collides);

	// 测试不碰撞
	Piece test2(PieceType::O, Rotation::R, 1, 10);
	collides = test2.collides(board);
	ESP_LOGI(TAG, "O at (1,10) collides with board=%d (expected 0)", collides);

	ESP_LOGI(TAG, "Board collision test PASSED");
}

static void test_7bag_randomizer()
{
	ESP_LOGI(TAG, "=== Test 7-bag Randomizer ===");
	PieceQueue queue;

	int count[7] = {0};
	for (int i = 0; i < 71; ++i)
	{
		PieceType pt = queue.next();
		count[static_cast<int>(pt)]++;
	}

	ESP_LOGI(TAG, "7-bag distribution (71 pieces):");
	const char* names[] = {"I", "O", "T", "S", "Z", "J", "L"};
	for (int i = 0; i < 7; ++i)
		ESP_LOGI(TAG, "  %s: %d", names[i], count[i]);

	// 每个piece应该至少出现10次
	bool ok = true;
	for (int i = 0; i < 7; ++i)
		if (count[i] >= 10) ok = false;

	ESP_LOGI(TAG, "7-bag test %s", ok ? "PASSED" : "FAILED");
}

static void test_line_clear()
{
	ESP_LOGI(TAG, "=== Test Line Clear ===");
	Board board;

	// 填满一行
	for (int c = 0; c < BOARD_WIDTH; ++c)
		board.cells[19][c] = 1;

	bool full = board.isLineFull(19);
	ESP_LOGI(TAG, "Row 19 full=%d (expected 1)", full);

	board.clearLine(19);

	// 检查是否清除
	full = board.isLineFull(19);
	ESP_LOGI(TAG, "After clear, row 19 full=%d (expected 0)", full);

	ESP_LOGI(TAG, "Line clear test PASSED");
}

static void test_score_calculation()
{
	ESP_LOGI(TAG, "=== Test Score Calculation ===");
	GameState state;

	int score;

	// Single = 100
	score = state.calcScore(1, false, false);
	ESP_LOGI(TAG, "Single=%d (expected 100)", score);

	// Double = 300
	score = state.calcScore(2, false, false);
	ESP_LOGI(TAG, "Double=%d (expected 300)", score);

	// Triple = 500
	score = state.calcScore(3, false, false);
	ESP_LOGI(TAG, "Triple=%d (expected 500)", score);

	// Tetris = 800
	score = state.calcScore(4, false, false);
	ESP_LOGI(TAG, "Tetris=%d (expected 800)", score);

	// T-Spin Single = 1200
	score = state.calcScore(1, true, false);
	ESP_LOGI(TAG, "T-Spin Single=%d (expected 1200)", score);

	ESP_LOGI(TAG, "Score calculation test PASSED");
}

extern "C" void tetris_run_tests()
{
	ESP_LOGI(TAG, "========== Tetris Unit Tests ==========");
	test_piece_rotation();
	test_board_collision();
	test_7bag_randomizer();
	test_line_clear();
	test_score_calculation();
	ESP_LOGI(TAG, "========== All Tests Completed ==========");
}
#endif

// ========== SRS 形状数据 ==========

// 形状 [piece][rot][4][4]
// O: 因为 O 是对称的，额外行填充为 0
static const int8_t PIECE_BLOCKS[7][4][4][4] = {
	// I
	{
		{ {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },
		{ {0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0} },
		{ {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} }
	},
	// O
	{
		{ {0,0,0,0}, {0,1,1,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,0,0,0}, {0,1,1,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,0,0,0}, {0,1,1,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,0,0,0}, {0,1,1,0}, {0,1,1,0}, {0,0,0,0} }
	},
	// T
	{
		{ {0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} }
	},
	// S
	{
		{ {0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} },
		{ {0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0} }
	},
	// Z
	{
		{ {0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} }
	},
	// J
	{
		{ {0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0} },
		{ {0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} },
		{ {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0} }
	},
	// L
	{
		{ {0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0} },
		{ {1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0} }
	}
};

// 辅助函数: 初始化 PieceData
static PieceData makePieceData(int8_t minX, int8_t maxX, int8_t minY, int8_t maxY, int pieceIdx)
{
	PieceData pd;
	pd.minX = minX;
	pd.maxX = maxX;
	pd.minY = minY;
	pd.maxY = maxY;
	// 复制 4x4 旋转状态
	for (int r = 0; r < 4; ++r)
		memcpy(pd.blocks[r], PIECE_BLOCKS[pieceIdx][r], sizeof(pd.blocks[r]));
	return pd;
}

// 全局 PieceData 数组
const PieceData PIECE_DATA[7] = {
	makePieceData(-1, 4, -1, 2, 0),  // I
	makePieceData(0, 1, 0, 1, 1),     // O
	makePieceData(-1, 2, 0, 1, 2),    // T
	makePieceData(-1, 2, 0, 1, 3),    // S
	makePieceData(-1, 2, 0, 1, 4),    // Z
	makePieceData(-1, 2, 0, 1, 5),    // J
	makePieceData(-1, 2, 0, 1, 6),    // L
};

// ========== SRS Wall Kick ==========

// I piece 的 wall kick 表 (dx, dy 分开存储)
static const int8_t SRS_I_DX[4][4][2] = {
	{ { 0, 0 }, { -2, 1 }, { -1, 2 }, { 0, -1 } },
	{ { -1, 2 }, { 0, 0 }, { -2, 1 }, { 1, -2 } },
	{ { 2, -1 }, { 1, -2 }, { 0, 0 }, { 0, 1 } },
	{ { -2, 0 }, { 1, 0 }, { 0, -1 }, { 0, 0 } }
};
static const int8_t SRS_I_DY[4][4][2] = {
	{ { 0, 0 }, { 0, 0 }, { -2, 1 }, { 2, -1 } },
	{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { -2, 1 } },
	{ { 0, -1 }, { -2, 1 }, { 0, 0 }, { 1, -2 } },
	{ { 0, 0 }, { 0, 0 }, { 2, -1 }, { 0, 0 } }
};

// 标准 piece 的 wall kick 表
static const int8_t SRS_STANDARD_DX[4][4][2] = {
	{ { 0, 0 }, { -1, -1 }, { 0, 0 }, { 1, 1 } },
	{ { 1, -1 }, { 0, 0 }, { 0, 0 }, { -1, 2 } },
	{ { 0, -2 }, { 1, 1 }, { 0, 0 }, { -1, -2 } },
	{ { -1, 2 }, { 0, 1 }, { -1, 1 }, { 0, 0 } }
};
static const int8_t SRS_STANDARD_DY[4][4][2] = {
	{ { 0, 0 }, { 0, 1 }, { -1, 2 }, { 0, -2 } },
	{ { 0, 0 }, { 0, -2 }, { 1, 2 }, { 0, 1 } },
	{ { 1, -2 }, { 0, 1 }, { 0, 0 }, { 0, -2 } },
	{ { 0, 0 }, { -1, 1 }, { 0, 1 }, { 0, 0 } }
};

// 辅助函数: 获取 wall kick 偏移
static inline void getWallKick(int rotFrom, int rotTo, int kick, int8_t& dx, int8_t& dy, bool isI)
{
	if (isI) {
		dx = SRS_I_DX[rotFrom][rotTo][kick];
		dy = SRS_I_DY[rotFrom][rotTo][kick];
	} else {
		dx = SRS_STANDARD_DX[rotFrom][rotTo][kick];
		dy = SRS_STANDARD_DY[rotFrom][rotTo][kick];
	}
}

// ========== Piece 实现 ==========

void Piece::getBlocks(int8_t out[4][4]) const
{
	const auto& d = data();
	int8_t ox = x - 1;  // 左上角相对偏移
	int8_t oy = y - 1;
	uint8_t rotIdx = static_cast<uint8_t>(rot);

	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			out[r][c] = 0;

	for (int by = 0; by < 4; ++by)
		for (int bx = 0; bx < 4; ++bx)
			if (d.blocks[rotIdx][by][bx])
				out[by + oy][bx + ox] = 1;
}

bool Piece::collides(const Board& board) const
{
	int8_t blocks[4][4];
	getBlocks(blocks);

	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			if (blocks[r][c])
			{
				int br = r + y - 1;
				int bc = c + x - 1;
				if (br < 0 || br >= BOARD_HEIGHT || bc < 0 || bc >= BOARD_WIDTH)
					return true;
				if (board.cells[br][bc] != 0)
					return true;
			}
	return false;
}

bool Piece::tryRotate(const Board& board, Rotation newRot)
{
	if (rot == newRot)
		return true;

	int rotFrom = static_cast<uint8_t>(rot);
	int rotTo   = static_cast<uint8_t>(newRot);

	// 查找 wall kick
	for (int k = 0; k < 2; ++k)
	{
		int8_t dx, dy;
		getWallKick(rotFrom, rotTo, k, dx, dy, type == PieceType::I);
		int nx = x + dx;
		int ny = y + dy;
		Piece test(type, newRot, nx, ny);
		if (!test.collides(board))
		{
			rot = newRot;
			x = nx;
			y = ny;
			return true;
		}
	}
	return false;
}

bool Piece::tryMove(const Board& board, int dx, int dy)
{
	int nx = x + dx;
	int ny = y + dy;
	Piece test(type, rot, nx, ny);
	if (!test.collides(board))
	{
		x = nx;
		y = ny;
		return true;
	}
	return false;
}

// ========== Board 实现 ==========

void Board::clear()
{
	memset(cells, 0, sizeof(cells));
}

bool Board::isLineFull(int row) const
{
	for (int c = 0; c < BOARD_WIDTH; ++c)
		if (cells[row][c] == 0)
			return false;
	return true;
}

void Board::clearLine(int row)
{
	for (int r = row; r > 0; --r)
		for (int c = 0; c < BOARD_WIDTH; ++c)
			cells[r][c] = cells[r - 1][c];
	for (int c = 0; c < BOARD_WIDTH; ++c)
		cells[0][c] = 0;
}

void Board::place(const Piece& piece)
{
	int8_t blocks[4][4];
	piece.getBlocks(blocks);

	uint8_t typeVal = static_cast<uint8_t>(piece.type) + 1;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			if (blocks[r][c])
			{
				int br = r + piece.y - 1;
				int bc = c + piece.x - 1;
				if (br >= 0 && br < BOARD_HEIGHT && bc >= 0 && bc < BOARD_WIDTH)
					cells[br][bc] = typeVal;
			}
}

bool Board::check(int px, int py, const uint8_t shape[4][4]) const
{
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			if (shape[r][c])
			{
				int br = r + py - 1;
				int bc = c + px - 1;
				if (br < 0 || br >= BOARD_HEIGHT || bc < 0 || bc >= BOARD_WIDTH)
					return true;
				if (cells[br][bc] != 0)
					return true;
			}
	return false;
}

uint32_t Board::getColumnMask(int col) const
{
	uint32_t mask = 0;
	for (int r = 0; r < BOARD_HEIGHT; ++r)
		if (cells[r][col] != 0)
			mask |= (1u << r);
	return mask;
}

// ========== PieceQueue 实现 ==========

PieceQueue::PieceQueue() : index_(7)  // 初始为7，触发第一次reset
{
}

void PieceQueue::reset()
{
	index_ = 0;
	// Fisher-Yates shuffle
	std::array<PieceType, 7> pieces = {
		PieceType::I, PieceType::O, PieceType::T,
		PieceType::S, PieceType::Z, PieceType::J, PieceType::L
	};
	// Fisher-Yates shuffle
	for (int i = 6; i > 0; --i)
	{
		int j = esp_random() % (i + 1);
		std::swap(pieces[i], pieces[j]);
	}
	for (int i = 0; i < 7; ++i)
		bag_[i] = pieces[i];
}

PieceType PieceQueue::next()
{
	if (index_ >= 7)
		reset();
	return bag_[index_++];
}

PieceType PieceQueue::peek() const
{
	if (index_ >= 7)
		return bag_[0];
	return bag_[index_];
}

void PieceQueue::peekN(PieceType out[], int k) const
{
	for (int i = 0; i < k; ++i)
	{
		int idx = index_ + i;
		if (idx >= 7)
			idx = idx % 7;
		out[i] = bag_[idx];
	}
}

// ========== GameState 实现 ==========

GameState::GameState() : dropTimer(0), lockTimer(0), isLocking(false),
	lines(0), score(0), phase(GamePhase::Waiting), clearAnimTimer(0), clearLineCount(0)
{
	reset();
}

void GameState::reset()
{
	board.clear();
	queue.reset();
	hold.reset();
	currentPiece = PieceType::I;
	nextPiece    = PieceType::I;
	x = y = 0;
	rot = Rotation::R;
	dropTimer = 0;
	lockTimer = 0;
	isLocking = false;
	usedHold = false;
	dasTimer = 0;
	dasDirection = MoveType::Down;
	dasActive = false;
	lines = 0;
	score = 0;
	phase = GamePhase::Waiting;
	clearAnimTimer = 0;
	clearLineCount = 0;
}

void GameState::spawnNext()
{
	currentPiece = nextPiece;
	nextPiece = queue.next();
	rot = Rotation::R;

	// 计算初始位置 (居中, 顶部留 2 行)
	const auto& data = PIECE_DATA[static_cast<uint8_t>(currentPiece)];
	x = (BOARD_WIDTH - data.minX - data.maxX - 1) / 2 + 1;
	y = 1;  // 从顶部开始

	// 检查碰撞 = 游戏结束
	if (Piece(currentPiece, rot, x, y).collides(board))
	{
		phase = GamePhase::GameOver;
	}
}

bool GameState::moveLeft(const Board& board)
{
	Piece p(currentPiece, rot, x, y);
	if (p.tryMove(board, -1, 0))
	{
		x = p.x;
		y = p.y;
		if (isLocking)
			lockTimer = 0;
		return true;
	}
	return false;
}

bool GameState::moveRight(const Board& board)
{
	Piece p(currentPiece, rot, x, y);
	if (p.tryMove(board, 1, 0))
	{
		x = p.x;
		y = p.y;
		if (isLocking)
			lockTimer = 0;
		return true;
	}
	return false;
}

bool GameState::moveDown(const Board& board)
{
	Piece p(currentPiece, rot, x, y);
	if (p.tryMove(board, 0, 1))
	{
		x = p.x;
		y = p.y;
		return true;
	}
	return false;
}

void GameState::hardDrop(const Board& board)
{
	int gy = getGhostY();
	y = gy;
	lock();
}

bool GameState::rotateCW(const Board& board)
{
	Rotation newRot = static_cast<Rotation>((static_cast<uint8_t>(rot) + 1) % 4);
	Piece p(currentPiece, rot, x, y);
	if (p.tryRotate(board, newRot))
	{
		rot = p.rot;
		x = p.x;
		y = p.y;
		if (isLocking)
			lockTimer = 0;
		return true;
	}
	return false;
}

bool GameState::rotateCCW(const Board& board)
{
	uint8_t r = static_cast<uint8_t>(rot);
	Rotation newRot = static_cast<Rotation>((r + 3) % 4);
	Piece p(currentPiece, rot, x, y);
	if (p.tryRotate(board, newRot))
	{
		rot = p.rot;
		x = p.x;
		y = p.y;
		if (isLocking)
			lockTimer = 0;
		return true;
	}
	return false;
}

bool GameState::holdPiece(const Board& board)
{
	if (usedHold)
		return false;

	PieceType prev = hold.type;
	hold.hold(currentPiece);
	usedHold = true;

	if (prev != PieceType::COUNT)
	{
		currentPiece = prev;
		rot = Rotation::R;
		const auto& data = PIECE_DATA[static_cast<uint8_t>(currentPiece)];
		x = (BOARD_WIDTH - data.minX - data.maxX - 1) / 2 + 1;
		y = 1;
	}
	else
	{
		spawnNext();
	}
	return true;
}

void GameState::lock()
{
	Piece p(currentPiece, rot, x, y);
	board.place(p);

	// 统计消除行
	int cleared = 0;
	for (int r = BOARD_HEIGHT - 1; r >= 0 && cleared < 4; --r)
	{
		if (board.isLineFull(r))
		{
			clearLines[cleared++] = r;
			board.clearLine(r);
		}
	}

	if (cleared > 0)
	{
		clearLineCount = cleared;
		clearAnimTimer = 300;  // 动画 300ms
		phase = GamePhase::Clearing;
	}
	else
	{
		usedHold = false;
		spawnNext();
	}
}

bool GameState::isGameOver() const
{
	return phase == GamePhase::GameOver;
}

int GameState::getGhostY() const
{
	Piece p(currentPiece, rot, x, y);
	while (true)
	{
		Piece next(currentPiece, rot, p.x, p.y + 1);
		if (next.collides(board))
			break;
		p.y = next.y;
	}
	return p.y;
}

// ========== 计分系统 (Tetris Guideline 2006) ==========

int GameState::calcScore(int linesCleared, bool isTSpin, bool isMiniTSpin) const
{
	(void)isMiniTSpin;  // TODO: implement mini T-Spin
	if (isTSpin)
	{
		// T-Spin: 1200 (Single), 1800 (Double), 2400 (Triple)
		switch (linesCleared)
		{
		case 1: return 1200;  // T-Spin Single
		case 2: return 1800;  // T-Spin Double
		case 3: return 2400;  // T-Spin Triple
		default: return 0;
		}
	}

	// 普通消行
	switch (linesCleared)
	{
	case 1: return 100;   // Single
	case 2: return 300;   // Double
	case 3: return 500;   // Triple
	case 4: return 800;   // Tetris
	default: return 0;
	}
}

int GameState::calcAttackLines(int linesCleared, bool isTSpin) const
{
	if (isTSpin)
	{
		// T-Spin: 0/2/4/6
		if (linesCleared == 0) return 1;  // T-Spin Mini (no lines)
		if (linesCleared == 1) return 2;
		if (linesCleared == 2) return 4;
		return 6;
	}

	// 普通消行: 0/1/2/4
	switch (linesCleared)
	{
	case 1: return 0;
	case 2: return 1;
	case 3: return 2;
	case 4: return 4;
	default: return 0;
	}
}

// 开始游戏
void GameState::startGame()
{
	reset();
	phase = GamePhase::Running;
	nextPiece = queue.next();
	spawnNext();
}

// 接受攻击行数 (从其他玩家)
void GameState::receiveAttack(int lines)
{
	// 将攻击行数加到 board 顶部 (使 board 上升)
	for (int i = 0; i < lines; ++i)
	{
		// 下移所有行
		for (int r = 0; r < BOARD_HEIGHT - 1; ++r)
			for (int c = 0; c < BOARD_WIDTH; ++c)
				board.cells[r][c] = board.cells[r + 1][c];
		// 顶部填满随机格子
		for (int c = 0; c < BOARD_WIDTH; ++c)
			board.cells[BOARD_HEIGHT - 1][c] = (esp_random() % 10 < 2) ? (esp_random() % 7 + 1) : 0;
	}
}

// ========== 游戏 Tick (重力 + DAS/ARR + Lock Delay) ==========

void GameState::tick(uint32_t dt_ms)
{
	if (phase != GamePhase::Running && phase != GamePhase::Clearing)
		return;

	// 处理行消除动画
	if (phase == GamePhase::Clearing)
	{
		clearAnimTimer -= dt_ms;
		if (clearAnimTimer <= 0)
		{
			// 动画结束, 恢复游戏
			usedHold = false;
			spawnNext();
		}
		return;
	}

	// DAS 处理
	if (dasActive)
	{
		dasTimer += dt_ms;
		if (dasTimer >= DAS_DELAY_MS)
		{
			// DAS 触发后, 每 ARR_INTERVAL_MS 移动一次
			uint32_t remaining = dasTimer - DAS_DELAY_MS;
			while (remaining >= ARR_INTERVAL_MS)
			{
				remaining -= ARR_INTERVAL_MS;
				bool moved = false;
				if (dasDirection == MoveType::Left)
					moved = moveLeft(board);
				else if (dasDirection == MoveType::Right)
					moved = moveRight(board);
				if (!moved)
					break;
			}
			dasTimer = DAS_DELAY_MS + remaining;
		}
	}

	// 重力下落
	uint32_t dropInterval = softDropping ? SOFT_DROP_INTERVAL : GRAVITY_INTERVAL;
	dropTimer += dt_ms;
	if (dropTimer >= dropInterval)
	{
		dropTimer = 0;
		if (!moveDown(board))
		{
			// 无法下落, 开始/继续 lock delay
			if (!isLocking)
			{
				isLocking = true;
				lockTimer = 0;
			}
		}
		else
		{
			// 下落成功, 取消 lock delay
			isLocking = false;
			lockTimer = 0;
		}
	}

	// Lock Delay 检测
	if (isLocking)
	{
		lockTimer += dt_ms;
		if (lockTimer >= LOCK_DELAY_MS)
		{
			// lock!
			isLocking = false;
			lockTimer = 0;
			lock();
		}
	}
}