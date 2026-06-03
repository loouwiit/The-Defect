#pragma once

// ============================================================
//  Tetris 公共类型定义
//  纯数据层，无 LVGL 依赖
// ============================================================

#include <cstdint>
#include <array>
#include <functional>

namespace tetris {

// 棋盘尺寸（Guideline 标准）
inline constexpr int kBoardWidth = 10;       // 棋盘宽（可视）
inline constexpr int kBoardHeight = 20;      // 棋盘高（可视）
inline constexpr int kHiddenHeight = 22;     // 含隐藏区总高

// 方块类型（7 种 Tetrimino）
enum class PieceType : uint8_t {
    I = 0, O, T, S, Z, J, L, None
};

inline constexpr int kPieceTypeCount = 7;

// 方块颜色（Guideline 标准配色，ARGB 8888）
inline constexpr uint32_t kColorEmpty = 0x00000000;     // 黑
inline constexpr uint32_t kColorI      = 0xFF00FFFF;    // 青 Cyan
inline constexpr uint32_t kColorO      = 0xFFFFFF00;    // 黄 Yellow
inline constexpr uint32_t kColorT      = 0xFFA000FF;    // 紫 Purple
inline constexpr uint32_t kColorS      = 0xFF00FF00;    // 绿 Green
inline constexpr uint32_t kColorZ      = 0xFFFF0000;    // 红 Red
inline constexpr uint32_t kColorJ      = 0xFF0000FF;    // 蓝 Blue
inline constexpr uint32_t kColorL      = 0xFFFF8000;    // 橙 Orange
inline constexpr uint32_t kColorGhost  = 0x40FFFFFF;    // 半透明白（幽灵块）

// 取方块颜色
inline uint32_t pieceColor(PieceType t) {
    switch (t) {
        case PieceType::I: return kColorI;
        case PieceType::O: return kColorO;
        case PieceType::T: return kColorT;
        case PieceType::S: return kColorS;
        case PieceType::Z: return kColorZ;
        case PieceType::J: return kColorJ;
        case PieceType::L: return kColorL;
        default: return kColorEmpty;
    }
}

// 旋转状态（0/1/2/3 = 0°/90°/180°/270°）
enum class Rotation : uint8_t { R0 = 0, R1, R2, R3 };

// 棋盘格子（颜色为 0 表示空）
using Cell = uint8_t;
inline constexpr Cell kCellEmpty = 0;

// 棋盘数据：行优先，y ∈ [0, kHiddenHeight)，x ∈ [0, kBoardWidth)
using BoardData = std::array<std::array<Cell, kBoardWidth>, kHiddenHeight>;

// 4×4 网格（一块方块的局部坐标）— 用 uint8 节省内存
using MiniBoard = std::array<std::array<Cell, 4>, 4>;

// 事件回调（TetrisClient → 外部）
struct GameEvent {
    enum class Type {
        PieceSpawn,       // 新方块出现
        PieceMove,        // 方块移动
        PieceRotate,      // 方块旋转
        PieceLock,        // 方块锁块
        LineClear,        // 消行
        HoldSwap,         // Hold 交换
        AddGarbage,       // 接收垃圾行
        GameOver,         // 死亡
        B2BTriggered,     // Back-to-Back 触发
    } type;
    int data = 0;          // 事件相关数据（消行数等）
};

// Piece 抽象（用于回调）
struct PieceSnapshot {
    PieceType type{ PieceType::None };
    Rotation rotation{ Rotation::R0 };
    int x{ 0 };
    int y{ 0 };
};

// 内部活动方块（与 PieceSnapshot 等价，用于引擎内部）
struct Piece {
    PieceType type{ PieceType::None };
    Rotation rotation{ Rotation::R0 };
    int x{ 0 };
    int y{ 0 };
};

}  // namespace tetris
