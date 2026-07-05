#include "tetris_client.hpp"
#include <esp_heap_caps.h>
#include <cstring>

// ============================================================
//  SRS 方块形状数据
//  SRS_SHAPES[type][rot][row][col]
//  1 = 方块存在, 0 = 空
// ============================================================

// 注意: 3×3 方块嵌入在 4×4 网格的第二行，第一行全部留空

#define R0 0
#define R1 1
#define R2 2
#define R3 3

// I piece
#define I_SHAPE_R0 {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}}
#define I_SHAPE_R1 {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}}
#define I_SHAPE_R2 {{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,1,1,1}}
#define I_SHAPE_R3 {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}

// O piece
#define O_SHAPE_R0 {{0,0,0,0},{1,1,0,0},{1,1,0,0},{0,0,0,0}}

// T piece
#define T_SHAPE_R0 {{0,0,0,0},{0,1,0,0},{1,1,1,0},{0,0,0,0}}
#define T_SHAPE_R1 {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,1,0,0}}
#define T_SHAPE_R2 {{0,0,0,0},{0,0,0,0},{1,1,1,0},{0,1,0,0}}
#define T_SHAPE_R3 {{0,0,0,0},{0,1,0,0},{1,1,0,0},{0,1,0,0}}

// S piece
#define S_SHAPE_R0 {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}}
#define S_SHAPE_R1 {{0,0,0,0},{0,1,0,0},{0,1,1,0},{0,0,1,0}}
#define S_SHAPE_R2 {{0,0,0,0},{0,0,0,0},{0,1,1,0},{1,1,0,0}}
#define S_SHAPE_R3 {{0,0,0,0},{1,0,0,0},{1,1,0,0},{0,1,0,0}}

// Z piece
#define Z_SHAPE_R0 {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}}
#define Z_SHAPE_R1 {{0,0,0,0},{0,0,1,0},{0,1,1,0},{0,1,0,0}}
#define Z_SHAPE_R2 {{0,0,0,0},{0,0,0,0},{1,1,0,0},{0,1,1,0}}
#define Z_SHAPE_R3 {{0,0,0,0},{0,1,0,0},{1,1,0,0},{1,0,0,0}}

// J piece
#define J_SHAPE_R0 {{0,0,0,0},{1,0,0,0},{1,1,1,0},{0,0,0,0}}
#define J_SHAPE_R1 {{0,0,0,0},{0,1,1,0},{0,1,0,0},{0,1,0,0}}
#define J_SHAPE_R2 {{0,0,0,0},{0,0,0,0},{1,1,1,0},{0,0,1,0}}
#define J_SHAPE_R3 {{0,0,0,0},{0,1,0,0},{0,1,0,0},{1,1,0,0}}

// L piece
#define L_SHAPE_R0 {{0,0,0,0},{0,0,1,0},{1,1,1,0},{0,0,0,0}}
#define L_SHAPE_R1 {{0,0,0,0},{0,1,0,0},{0,1,0,0},{0,1,1,0}}
#define L_SHAPE_R2 {{0,0,0,0},{0,0,0,0},{1,1,1,0},{1,0,0,0}}
#define L_SHAPE_R3 {{0,0,0,0},{1,1,0,0},{0,1,0,0},{0,1,0,0}}

const uint8_t SRS_SHAPES[7][4][4][4] = {
    { I_SHAPE_R0, I_SHAPE_R1, I_SHAPE_R2, I_SHAPE_R3 },  // I
    { O_SHAPE_R0, O_SHAPE_R0, O_SHAPE_R0, O_SHAPE_R0 },  // O
    { T_SHAPE_R0, T_SHAPE_R1, T_SHAPE_R2, T_SHAPE_R3 },  // T
    { S_SHAPE_R0, S_SHAPE_R1, S_SHAPE_R2, S_SHAPE_R3 },  // S
    { Z_SHAPE_R0, Z_SHAPE_R1, Z_SHAPE_R2, Z_SHAPE_R3 },  // Z
    { J_SHAPE_R0, J_SHAPE_R1, J_SHAPE_R2, J_SHAPE_R3 },  // J
    { L_SHAPE_R0, L_SHAPE_R1, L_SHAPE_R2, L_SHAPE_R3 },  // L
};

#undef R0
#undef R1
#undef R2
#undef R3

// ============================================================
//  SRS Wall Kick 数据
//
//  索引方式: kicks[from][to][test]
//  from, to: 0=R0, 1=R1(CW), 2=R2, 3=R3(CCW)
//  对于不存在的旋转对（如 0→2 需通过 0→R→2 两次调用），
//  填充为 {0,0} 不应被使用。
//
//  参考: http://tetriswiki.cn/p/超级旋转系统
// ============================================================

// JLSTZ 共用踢墙表
// 值直接使用 SRS 规范（y+ = 上，无需转换）
const KickOffset JLSTZ_KICKS[4][4][5] = {
    // from R0 (0)
    {   // → R0 (identity)
        {{0,0},{0,0},{0,0},{0,0},{0,0}},
        // → R1 (0→R)
        {{0,0},{-1,0},{-1,+1},{0,-2},{-1,-2}},
        // → R2 (0→2) — 不直接使用
        {{0,0},{0,0},{0,0},{0,0},{0,0}},
        // → R3 (0→L)
        {{0,0},{+1,0},{+1,+1},{0,-2},{+1,-2}}
    },
    // from R1 (R)
    {   // → R0 (R→0)
        {{0,0},{+1,0},{+1,-1},{0,+2},{+1,+2}},
        // → R1 (identity)
        {{0,0},{0,0},{0,0},{0,0},{0,0}},
        // → R2 (R→2)
        {{0,0},{+1,0},{+1,-1},{0,+2},{+1,+2}},
        // → R3 (R→L) — 不直接使用
        {{0,0},{0,0},{0,0},{0,0},{0,0}}
    },
    // from R2 (2)
    {   // → R0 (2→0) — 不直接使用
        {{0,0},{0,0},{0,0},{0,0},{0,0}},
        // → R1 (2→R)
        {{0,0},{-1,0},{-1,+1},{0,-2},{-1,-2}},
        // → R2 (identity)
        {{0,0},{0,0},{0,0},{0,0},{0,0}},
        // → R3 (2→L)
        {{0,0},{+1,0},{+1,+1},{0,-2},{+1,-2}}
    },
    // from R3 (L)
    {   // → R0 (L→0)
        {{0,0},{-1,0},{-1,-1},{0,+2},{-1,+2}},
        // → R1 (L→R) — 不直接使用
        {{0,0},{0,0},{0,0},{0,0},{0,0}},
        // → R2 (L→2)
        {{0,0},{-1,0},{-1,-1},{0,+2},{-1,+2}},
        // → R3 (identity)
        {{0,0},{0,0},{0,0},{0,0},{0,0}}
    }
};

// I 专用踢墙表（SRS 标准，y+ = 上）
const KickOffset I_KICKS[4][4][5] = {
    // from R0 (0)
    {
        {{0,0},{0,0},{0,0},{0,0},{0,0}},           // → R0
        {{0,0},{-2,0},{+1,0},{-2,-1},{+1,+2}},     // → R1
        {{0,0},{0,0},{0,0},{0,0},{0,0}},           // → R2
        {{0,0},{-1,0},{+2,0},{-1,+2},{+2,-1}}      // → R3
    },
    // from R1 (R)
    {
        {{0,0},{+2,0},{-1,0},{+2,+1},{-1,-2}},     // → R0
        {{0,0},{0,0},{0,0},{0,0},{0,0}},           // → R1
        {{0,0},{-1,0},{+2,0},{-1,+2},{+2,-1}},     // → R2
        {{0,0},{0,0},{0,0},{0,0},{0,0}}            // → R3
    },
    // from R2 (2)
    {
        {{0,0},{0,0},{0,0},{0,0},{0,0}},           // → R0
        {{0,0},{+1,0},{-2,0},{+1,-2},{-2,+1}},     // → R1
        {{0,0},{0,0},{0,0},{0,0},{0,0}},           // → R2
        {{0,0},{+2,0},{-1,0},{+2,+1},{-1,-2}}      // → R3
    },
    // from R3 (L)
    {
        {{0,0},{+1,0},{-2,0},{+1,-2},{-2,+1}},     // → R0
        {{0,0},{0,0},{0,0},{0,0},{0,0}},           // → R1
        {{0,0},{-2,0},{+1,0},{-2,-1},{+1,+2}},     // → R2
        {{0,0},{0,0},{0,0},{0,0},{0,0}}            // → R3
    }
};

// ============================================================
//  Piece 实现
// ============================================================

Piece::Piece(PieceType type, Rotation rot, int x, int y)
    : m_type(type), m_rot(rot), m_x(x), m_y(y)
{
}

void Piece::getBlocks(int outCols[4], int outRows[4]) const
{
    if (m_type == PieceType::NONE) {
        for (int i = 0; i < 4; i++) {
            outCols[i] = outRows[i] = 0;
        }
        return;
    }

    int idx = 0;
    const auto& shape = SRS_SHAPES[static_cast<int>(m_type)][static_cast<int>(m_rot)];
    for (int row = 0; row < PIECE_SIZE && idx < 4; row++) {
        for (int col = 0; col < PIECE_SIZE && idx < 4; col++) {
            if (shape[row][col]) {
                outCols[idx] = m_x + col;
                // row=0 = 形状顶部 → y 最大（C++ y-up）, row=3 = 形状底部 → y 最小
                outRows[idx] = m_y + (PIECE_SIZE - 1 - row);
                idx++;
            }
        }
    }
}

Piece Piece::rotatedCW() const
{
    Rotation newRot = static_cast<Rotation>((static_cast<int>(m_rot) + 1) & 3);
    return Piece(m_type, newRot, m_x, m_y);
}

Piece Piece::rotatedCCW() const
{
    Rotation newRot = static_cast<Rotation>((static_cast<int>(m_rot) + 3) & 3);
    return Piece(m_type, newRot, m_x, m_y);
}

Piece Piece::moved(int dx, int dy) const
{
    return Piece(m_type, m_rot, m_x + dx, m_y + dy);
}

// ============================================================
//  Board 实现
// ============================================================

Board::Board()
{
    clear();
}

void Board::clear()
{
    std::memset(m_cells, 0, sizeof(m_cells));
}

BoardCell Board::get(int col, int y) const
{
    // 墙或底部碰撞
    if (col < 0 || col >= BOARD_WIDTH || y < 0)
        return 1;
    // 允许 y >= BOARD_HEIGHT（在棋盘上方，生成区）
    if (y >= BOARD_HEIGHT)
        return 0;
    return m_cells[yToRow(y) * BOARD_WIDTH + col];
}

void Board::set(int col, int y, BoardCell val)
{
    if (col < 0 || col >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT)
        return;
    m_cells[yToRow(y) * BOARD_WIDTH + col] = val;
}

bool Board::collides(const Piece& piece) const
{
    int cols[4], yCoords[4];
    piece.getBlocks(cols, yCoords);

    for (int i = 0; i < 4; i++) {
        int c = cols[i];
        int y = yCoords[i];

        // 左右墙或底部碰撞
        if (c < 0 || c >= BOARD_WIDTH || y < 0)
            return true;

        // 允许 y >= BOARD_HEIGHT（在棋盘上方，生成区）
        if (y >= BOARD_HEIGHT)
            continue;

        // 已有格子
        if (m_cells[yToRow(y) * BOARD_WIDTH + c] != 0)
            return true;
    }
    return false;
}

void Board::place(const Piece& piece, BoardCell val)
{
    int cols[4], yCoords[4];
    piece.getBlocks(cols, yCoords);

    for (int i = 0; i < 4; i++) {
        int c = cols[i];
        int y = yCoords[i];
        if (c >= 0 && c < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT) {
            m_cells[yToRow(y) * BOARD_WIDTH + c] = val;
        }
    }
}

bool Board::isLineFull(int y) const
{
    if (y < 0 || y >= BOARD_HEIGHT) return false;
    int row = yToRow(y);
    for (int col = 0; col < BOARD_WIDTH; col++) {
        if (m_cells[row * BOARD_WIDTH + col] == 0)
            return false;
    }
    return true;
}

bool Board::isRowEmpty(int y) const
{
    if (y < 0 || y >= BOARD_HEIGHT) return true;
    int row = yToRow(y);
    for (int col = 0; col < BOARD_WIDTH; col++) {
        if (m_cells[row * BOARD_WIDTH + col] != 0)
            return false;
    }
    return true;
}

int Board::clearLines(int clearedY[4])
{
    int count = 0;

    // 用内部行号从底向上扫描 (array 底 = BOARD_HEIGHT-1 = y=0)
    for (int row = BOARD_HEIGHT - 1; row >= 0 && count < 4; row--) {
        if (isLineFull(rowToY(row))) {
            clearedY[count] = rowToY(row);  // 转换为 y 坐标
            count++;
        }
    }

    if (count == 0) return 0;

    // 从底向上消除并下移（内部行号操作不变）
    int writeRow = BOARD_HEIGHT - 1;
    for (int readRow = BOARD_HEIGHT - 1; readRow >= 0; readRow--) {
        if (isLineFull(rowToY(readRow)))
            continue;  // 跳过满行

        if (writeRow != readRow) {
            std::memcpy(
                &m_cells[writeRow * BOARD_WIDTH],
                &m_cells[readRow * BOARD_WIDTH],
                BOARD_WIDTH
            );
        }
        writeRow--;
    }

    // 顶部填充空行
    while (writeRow >= 0) {
        std::memset(&m_cells[writeRow * BOARD_WIDTH], 0, BOARD_WIDTH);
        writeRow--;
    }

    return count;
}

void Board::addGarbage(int lines, int colHole)
{
    if (lines <= 0) return;
    if (lines > BOARD_HEIGHT) lines = BOARD_HEIGHT;

    // 棋盘内容向上平移（向 y 正方向 = 向 array 小索引方向）
    // y=0=底=array 底，垃圾行插入底部
    // 丢弃顶部 lines 行，底部新填 lines 行垃圾
    int moveSize = (BOARD_HEIGHT - lines) * BOARD_WIDTH;
    std::memmove(m_cells, &m_cells[lines * BOARD_WIDTH], moveSize);

    // 底部填充垃圾行 (y=0 ~ y=lines-1 = 内部行 BOARD_HEIGHT-lines ~ BOARD_HEIGHT-1)
    for (int r = BOARD_HEIGHT - lines; r < BOARD_HEIGHT; r++) {
        for (int c = 0; c < BOARD_WIDTH; c++) {
            m_cells[r * BOARD_WIDTH + c] = (c == colHole) ? 0 : 8;  // 8 = 垃圾行标记
        }
    }
}

uint16_t Board::getColumnMask(int y) const
{
    if (y < 0 || y >= BOARD_HEIGHT) return 0;
    int row = yToRow(y);
    uint16_t mask = 0;
    for (int col = 0; col < BOARD_WIDTH; col++) {
        if (m_cells[row * BOARD_WIDTH + col] != 0)
            mask |= (1 << col);
    }
    return mask;
}

// ============================================================
//  PieceQueue 实现 (7-bag 动态链表，按索引自增长)
// ============================================================

static FastRng s_rng(12345678);

PieceQueue::PieceQueue()
{
    reset();
}

PieceQueue::~PieceQueue()
{
    freeAll();
}

void PieceQueue::reset()
{
    freeAll();

    m_head = allocNode();
    if (m_head) {
        shuffleBag(m_head->pieces);
        m_tail = m_head;
        m_totalNodes = 1;
    }
}

PieceQueue::Node* PieceQueue::allocNode()
{
    auto* node = static_cast<Node*>(
        heap_caps_malloc(sizeof(Node), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (node) {
        node->next = nullptr;
    }
    return node;
}

void PieceQueue::freeAll()
{
    Node* node = m_head;
    while (node) {
        Node* next = node->next;
        heap_caps_free(node);
        node = next;
    }
    m_head = nullptr;
    m_tail = nullptr;
    m_totalNodes = 0;
}

void PieceQueue::shuffleBag(PieceType* bag)
{
    for (int i = 0; i < 7; i++)
        bag[i] = static_cast<PieceType>(i);

    for (int i = 6; i > 0; i--) {
        int j = s_rng.range(i + 1);
        std::swap(bag[i], bag[j]);
    }
}

void PieceQueue::ensureAtLeast(int totalNodes)
{
    while (m_totalNodes < totalNodes) {
        Node* node = allocNode();
        if (!node) break;  // OOM 保护
        shuffleBag(node->pieces);
        if (m_tail) {
            m_tail->next = node;
        }
        else {
            m_head = node;
        }
        m_tail = node;
        m_totalNodes++;
    }
}

PieceType PieceQueue::peek(int index)
{
    int nodeIdx = index / 7;
    ensureAtLeast(nodeIdx + 1);  // 保证目标节点存在

    Node* node = m_head;
    for (int i = 0; i < nodeIdx; i++) {
        if (!node) return PieceType::NONE;
        node = node->next;
    }
    if (!node) return PieceType::NONE;

    return node->pieces[index % 7];
}

// ============================================================
//  Scoring 实现
// ============================================================

void Scoring::reset()
{
    m_score = 0;
    m_level = 0;
    m_totalLines = 0;
    m_combo = -1;
    m_b2b = false;
}

int Scoring::processLines(int lines, bool isTSpin, bool isTSpinMini,
    int softDropRows, int hardDropRows)
{
    int earned = 0;

    // Soft/Hard drop 加分
    earned += softDropRows * SCORE_SOFT_DROP;
    earned += hardDropRows * SCORE_HARD_DROP;

    if (lines == 0) {
        // Reset combo
        m_combo = -1;
        return earned;
    }

    // 基础得分
    int baseScore = 0;

    if (isTSpin) {
        if (isTSpinMini) {
            switch (lines) {
            case 1: baseScore = SCORE_TSPIN_MINI;    break;
            case 2: baseScore = SCORE_TSPIN_SINGLE;  break;
            default: break;
            }
        }
        else {
            switch (lines) {
            case 1: baseScore = SCORE_TSPIN_SINGLE;  break;
            case 2: baseScore = SCORE_TSPIN_DOUBLE;  break;
            case 3: baseScore = SCORE_TSPIN_TRIPLE;  break;
            default: break;
            }
        }
    }
    else {
        switch (lines) {
        case 1: baseScore = SCORE_SINGLE;  break;
        case 2: baseScore = SCORE_DOUBLE;  break;
        case 3: baseScore = SCORE_TRIPLE;  break;
        case 4: baseScore = SCORE_TETRIS;  break;
        default: break;
        }
    }

    // Back-to-Back
    bool isDifficult = (lines == 4) || isTSpin;
    if (isDifficult) {
        if (m_b2b) {
            baseScore = baseScore * SCORE_B2B_MULTIPLIER / SCORE_B2B_DIVISOR;
        }
        m_b2b = true;
    }
    else {
        m_b2b = false;
    }

    // Combo
    m_combo++;
    int comboBonus = 0;
    if (m_combo > 0)
        comboBonus = SCORE_COMBO_BASE * m_combo;

    // Level 倍率
    int levelMultiplier = (m_level == 0) ? 1 : m_level;

    earned += (baseScore + comboBonus) * levelMultiplier;

    // 更新状态
    m_score += earned;
    m_totalLines += lines;
    m_level = m_totalLines / 10;

    return earned;
}

// ============================================================
//  三角判定（T-Spin 几何条件）
// ============================================================

static bool isOccupied(const Board& board, int col, int y)
{
    // 墙壁算被占用
    if (col < 0 || col >= BOARD_WIDTH || y < 0)
        return true;
    // 棋盘上方算空
    if (y >= BOARD_HEIGHT)
        return false;
    return board.get(col, y) != 0;
}

bool checkThreeCorner(const Piece& piece, const Board& board)
{
    // 只有 T 块能触发
    if (piece.type() != PieceType::T)
        return false;

    int x = piece.x();
    int y = piece.y();

    // 3×3 包围盒的 4 个角
    // TL = (x, y),   TR = (x+2, y)
    // BL = (x, y+2), BR = (x+2, y+2)
    int corners = 0;
    if (isOccupied(board, x, y))     corners++;
    if (isOccupied(board, x + 2, y))     corners++;
    if (isOccupied(board, x, y + 2)) corners++;
    if (isOccupied(board, x + 2, y + 2)) corners++;

    return corners >= 3;
}

// ============================================================
//  攻击行计算
// ============================================================

int calcAttackLines(int linesCleared, bool isTSpin, bool isTSpinMini, bool isB2B, int combo)
{
    int attack = 0;

    if (isTSpin) {
        if (isTSpinMini) {
            attack = (linesCleared == 2) ? 2 : 0;
        }
        else {
            switch (linesCleared) {
            case 1: attack = 2; break;
            case 2: attack = 4; break;
            case 3: attack = 6; break;
            default: break;
            }
        }
    }
    else {
        switch (linesCleared) {
        case 1: attack = 0; break;  // Single: 0 垃圾行
        case 2: attack = 1; break;  // Double: 1
        case 3: attack = 2; break;  // Triple: 2
        case 4: attack = 4; break;  // Tetris: 4
        default: break;
        }
    }

    // Back-to-Back 加成 (+1)
    if (isB2B && attack > 0)
        attack++;

    // Combo 加成（连续消行每多一次 +combo 攻击）
    if (combo > 0)
        attack += combo;

    return attack;
}

// ============================================================
//  Ghost Piece 计算
// ============================================================

Piece calculateGhost(const Piece& piece, const Board& board)
{
    Piece ghost = piece;
    while (!board.collides(ghost.moved(0, -1)))  // 向下 (y 递减)
        ghost = ghost.moved(0, -1);
    return ghost;
}
