#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <algorithm>

/* ============================================================
 *  Tetris Client — 纯游戏逻辑
 *
 *  包含：Piece / Board / PieceQueue / Scoring / GameState
 *  不依赖 LVGL，纯 C++17 数据结构和算法。
 *
 *  坐标系：
 *    x: 0=左, 9=右
 *    y: 0=底(可见行最下), 21=顶(隐藏区最上)
 *    棋盘 10 列 × 22 行（含 2 行隐藏区），可见 10×20 (y=0~19)
 *    方块生成在顶部的隐藏区 (y=20~21)，下落到 y=0
 *    与 SRS 标准一致：y 正方向 = 上
 *
 *  参考：
 *    SRS: http://tetriswiki.cn/p/超级旋转系统
 *    Guideline Scoring: Tetris Guideline 2006
 * ============================================================ */

// ============================================================
//  常量
// ============================================================

constexpr int BOARD_WIDTH  = 10;
constexpr int BOARD_HEIGHT = 22;      // 总行数（含 2 行隐藏区）
constexpr int BOARD_VISIBLE_H = 20;   // 可见行数 (y=0~19)
constexpr int BOARD_HIDDEN_H  = 2;    // 隐藏行数 (y=20~21)

constexpr int PIECE_SIZE = 4;         // 4×4 包围盒

constexpr int DAS_DELAY_MS  = 170;    // Delayed Auto Shift
constexpr int ARR_RATE_MS   = 50;     // Auto Repeat Rate
constexpr int LOCK_DELAY_MS = 3000;   // 锁定延迟（ms，每次移动/旋转重置）
constexpr int SOFT_DROP_MS  = 50;     // 软降间隔

// ============================================================
//  枚举
// ============================================================

enum class PieceType : uint8_t {
    I = 0, O, T, S, Z, J, L,
    COUNT,
    NONE = 0xFF
};

enum class Rotation : uint8_t {
    R0 = 0,   // 初始态 (0)
    R1 = 1,   // 顺时针 90° (R)
    R2 = 2,   // 180° (2)
    R3 = 3,   // 逆时针 90° (L)
};

// ============================================================
//  SRS 方块形状数据 (4×4 网格)
// ============================================================

// blocks[pieceType][rotation][row][col]
// row=0 是顶部，col=0 是左侧
extern const uint8_t SRS_SHAPES[7][4][4][4];

// ============================================================
//  SRS Wall Kick 数据
// ============================================================

// kickCount[from][to] = 5 (含基本旋转 (0,0))
// kicks 数组: 5 组 (dx, dy)
// dx: 正值=右, 负值=左
// dy: 正值=上, 负值=下
// 与 SRS 标准一致：y 向上为正

struct KickOffset { int8_t dx, dy; };

// JLSTZ 共用踢墙表
extern const KickOffset JLSTZ_KICKS[4][4][5];
// I 专用踢墙表
extern const KickOffset I_KICKS[4][4][5];

// ============================================================
//  Piece 类
// ============================================================

class Piece {
public:
    Piece() = default;
    Piece(PieceType type, Rotation rot = Rotation::R0, int x = 3, int y = BOARD_HEIGHT - 2);

    // 访问
    PieceType  type()     const { return m_type; }
    Rotation   rotation() const { return m_rot; }
    int        x()        const { return m_x; }
    int        y()        const { return m_y; }

    void setX(int x) { m_x = x; }
    void setY(int y) { m_y = y; }

    // 获取当前 4 个方块的 (col, row) 坐标（相对于 board）
    void getBlocks(int outCols[4], int outRows[4]) const;

    // 旋转（返回新的 Piece，不修改自身）
    Piece rotatedCW()  const;   // 顺时针
    Piece rotatedCCW() const;   // 逆时针

    // 移动（返回新的 Piece）
    Piece moved(int dx, int dy) const;

private:
    PieceType m_type = PieceType::NONE;
    Rotation  m_rot  = Rotation::R0;
    int       m_x    = 0;
    int       m_y    = 0;
};

// ============================================================
//  Board 类
// ============================================================

// 格子值: 0=空, 1~7 对应 PieceType+1
using BoardCell = uint8_t;

class Board {
public:
    Board();

    // 清空棋盘
    void clear();

    // 坐标转换: y (底=0, 顶=BOARD_HEIGHT-1) → 内部数组行号 (顶=0, 底=BOARD_HEIGHT-1)
    static int yToRow(int y) { return BOARD_HEIGHT - 1 - y; }
    static int rowToY(int row) { return BOARD_HEIGHT - 1 - row; }

    // 访问格子 (y: 0=底, 21=顶)
    BoardCell get(int col, int y) const;
    void      set(int col, int y, BoardCell val);

    // 碰撞检测: piece 的 4 个格子是否与已有格子或边界冲突
    bool collides(const Piece& piece) const;

    // 放置 piece 到棋盘 (不检测碰撞)
    void place(const Piece& piece, BoardCell val);

    // 检测并消除满行，返回消除的行数
    // clearedY 输出被清除行的 y 坐标 (最多4行)
    int clearLines(int clearedY[4]);

    // 垃圾行注入
    // 在棋盘底部插入 lines 行，每行在 colHole 处留空
    // 棋盘内容向上平移，顶部行被丢弃
    void addGarbage(int lines, int colHole);

    // 检查某行是否全满 (y: 0=底)
    bool isLineFull(int y) const;

    // 获取列掩码 (供渲染器使用, y: 0=底)
    uint16_t getColumnMask(int y) const;

    // 原始数据访问 (只读，内部行号: 0=顶, BOARD_HEIGHT-1=底)
    const BoardCell* data() const { return m_cells; }

    // 检查给定行是否全部为空 (y: 0=底)
    bool isRowEmpty(int y) const;

private:
    // m_cells[row * BOARD_WIDTH + col], row=0=顶, row=21=底
    BoardCell m_cells[BOARD_HEIGHT * BOARD_WIDTH]{};
};

// ============================================================
//  PieceQueue — 7-bag Randomizer (链表)
// ============================================================

// 一个 bag 节点，包含 7 个洗牌后的方块和指向下一 bag 的指针
struct BagNode {
    PieceType pieces[7];
    BagNode* next = nullptr;
};

class PieceQueue {
public:
    PieceQueue();
    ~PieceQueue();

    void reset();

    // 取下一个方块
    PieceType next();

    // 预览: peek(i) 返回接下来第 i 个 (0=下一个)，可跨 bag
    PieceType peek(int index) const;
    PieceType peek() const { return peek(0); }

    // 当前 bag 中剩余数量
    int remaining() const { return 7 - m_pos; }

    // 序列化: 将剩余队列写入 out，返回写入数量（用于网络同步）
    int serialize(PieceType* out, int maxCount) const;

private:
    BagNode* allocBag();                // 从池中分配一个 bag
    void shuffleBag(PieceType* bag);    // Fisher-Yates 洗牌
    void ensureAhead();                 // 确保后续有足够的 bag

    BagNode* m_head = nullptr;
    int m_pos = 0;          // 0-6，当前 bag 中的位置

    // 固定池，避免运行时动态分配
    static constexpr int POOL_SIZE = 4;
    BagNode m_pool[POOL_SIZE]{};
    int m_poolNext = 0;     // 下一个可用池索引
};

// ============================================================
//  Scoring — 计分系统
// ============================================================

// 消行得分
constexpr int SCORE_SINGLE  = 100;
constexpr int SCORE_DOUBLE  = 300;
constexpr int SCORE_TRIPLE  = 500;
constexpr int SCORE_TETRIS  = 800;

// T-Spin 加成
constexpr int SCORE_TSPIN_MINI  = 100;  // T-Spin Mini
constexpr int SCORE_TSPIN_SINGLE = 800;
constexpr int SCORE_TSPIN_DOUBLE = 1200;
constexpr int SCORE_TSPIN_TRIPLE = 1600;

// Soft drop 加分 (每行)
constexpr int SCORE_SOFT_DROP = 1;
// Hard drop 加分 (每行)
constexpr int SCORE_HARD_DROP = 2;

// Back-to-Back 加成 (×1.5)
constexpr int SCORE_B2B_MULTIPLIER = 3;  // 实际 ×1.5, 用分子3/2
constexpr int SCORE_B2B_DIVISOR    = 2;

// Combo 加成 (连续消行，n=combo count)
// combo >= 1: 50 × combo
constexpr int SCORE_COMBO_BASE = 50;

class Scoring {
public:
    Scoring() = default;

    // 重置
    void reset();

    // 处理一次消行事件
    // lines: 消行数 (0-4)
    // isTSpin: 是否为 T-Spin
    // isTSpinMini: 是否为 T-Spin Mini
    // softDropRows: 本次软降行数
    // hardDropRows: 本次硬降行数
    // 返回本次获得的分数
    int processLines(int lines, bool isTSpin, bool isTSpinMini,
                     int softDropRows = 0, int hardDropRows = 0);

    // 获取状态
    int  score()      const { return m_score; }
    int  level()      const { return m_level; }
    int  totalLines() const { return m_totalLines; }
    int  combo()      const { return m_combo; }
    bool isB2B()      const { return m_b2b; }

private:
    int m_score      = 0;
    int m_level      = 0;      // 起始 Level 0 (或 1? 规范通常 1)
    int m_totalLines = 0;
    int m_combo      = -1;     // -1 = 无 combo
    bool m_b2b       = false;  // Back-to-Back 状态
};

// ============================================================
//  攻击行计算
// ============================================================

// 根据消行情况计算发送的垃圾行数
// 返回攻击行数 (用于发送给对手)
int calcAttackLines(int linesCleared, bool isTSpin, bool isTSpinMini, bool isB2B, int combo = 0);

// ============================================================
//  Ghost Piece 计算
// ============================================================

// 计算幽灵块位置 (硬降到底)
Piece calculateGhost(const Piece& piece, const Board& board);

// ============================================================
//  T-Spin 检测
// ============================================================

// 检测 T 块是否是三角 T-Spin
// piece: 刚锁定的 T 块（尚未放置到棋盘）
// board: 当前棋盘（不含 piece）
// 返回: T 块 3×3 包围盒的 4 个角中 ≥ 3 个被占用
//
// 注意：此函数仅检查几何条件，旋转进入的判断由调用方负责
bool checkThreeCorner(const Piece& piece, const Board& board);

// ============================================================
//  辅助函数
// ============================================================

// 从 PieceType 获取默认颜色值 (1-7)
constexpr BoardCell pieceToColor(PieceType type) {
    return static_cast<BoardCell>(type) + 1;
}

// 从颜色值获取 PieceType
constexpr PieceType colorToPiece(BoardCell color) {
    if (color == 0) return PieceType::NONE;
    return static_cast<PieceType>(color - 1);
}

// 随机数生成器 (Xorshift32, 轻量)
class FastRng {
public:
    explicit FastRng(uint32_t seed = 0x12345678) : m_state(seed) {}

    uint32_t next() {
        uint32_t x = m_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        m_state = x;
        return x;
    }

    // 返回 [0, max) 范围内的整数
    int range(int max) {
        return static_cast<int>(next() % max);
    }

private:
    uint32_t m_state;
};
