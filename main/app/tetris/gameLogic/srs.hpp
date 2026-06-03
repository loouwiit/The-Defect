#pragma once

// ============================================================
//  SRS (Super Rotation System) 旋转 + Wall Kick 数据
//  来自 Tetris Guideline 2006 规范
// ============================================================

#include "tetrisTypes.hpp"

namespace tetris::srs {

// 7 种方块的 4×4 旋转状态
// 坐标: (0,0) 在左上，x→右，y→下
// 来源: https://tetris.fandom.com/wiki/SRS

// I-piece 旋转状态 (spawn 时中心在 x=3, y=0；O 同理)
inline constexpr MiniBoard kI_R0 = [] {
    MiniBoard b{};
    b[1][0] = 1; b[1][1] = 1; b[1][2] = 1; b[1][3] = 1;
    return b;
}();
inline constexpr MiniBoard kI_R1 = [] {
    MiniBoard b{};
    b[0][2] = 1; b[1][2] = 1; b[2][2] = 1; b[3][2] = 1;
    return b;
}();
inline constexpr MiniBoard kI_R2 = [] {
    MiniBoard b{};
    b[2][0] = 1; b[2][1] = 1; b[2][2] = 1; b[2][3] = 1;
    return b;
}();
inline constexpr MiniBoard kI_R3 = [] {
    MiniBoard b{};
    b[0][1] = 1; b[1][1] = 1; b[2][1] = 1; b[3][1] = 1;
    return b;
}();

// O-piece (4 种旋转状态都是相同的 2×2)
inline constexpr MiniBoard kO = [] {
    MiniBoard b{};
    b[1][1] = 1; b[1][2] = 1; b[2][1] = 1; b[2][2] = 1;
    return b;
}();

// T-piece
inline constexpr MiniBoard kT_R0 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[1][2] = 1; b[1][3] = 1; b[2][2] = 1;
    return b;
}();
inline constexpr MiniBoard kT_R1 = [] {
    MiniBoard b{};
    b[1][2] = 1; b[2][2] = 1; b[2][3] = 1; b[3][2] = 1;
    return b;
}();
inline constexpr MiniBoard kT_R2 = [] {
    MiniBoard b{};
    b[2][1] = 1; b[2][2] = 1; b[2][3] = 1; b[1][2] = 1;
    return b;
}();
inline constexpr MiniBoard kT_R3 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[2][1] = 1; b[2][2] = 1; b[3][1] = 1;
    return b;
}();

// S-piece
inline constexpr MiniBoard kS_R0 = [] {
    MiniBoard b{};
    b[1][2] = 1; b[1][3] = 1; b[2][1] = 1; b[2][2] = 1;
    return b;
}();
inline constexpr MiniBoard kS_R1 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[2][1] = 1; b[2][2] = 1; b[3][2] = 1;
    return b;
}();
inline constexpr MiniBoard kS_R2 = kS_R0;  // S 180° 与 0° 相同
inline constexpr MiniBoard kS_R3 = kS_R1;

// Z-piece
inline constexpr MiniBoard kZ_R0 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[1][2] = 1; b[2][2] = 1; b[2][3] = 1;
    return b;
}();
inline constexpr MiniBoard kZ_R1 = [] {
    MiniBoard b{};
    b[1][2] = 1; b[2][1] = 1; b[2][2] = 1; b[3][1] = 1;
    return b;
}();
inline constexpr MiniBoard kZ_R2 = kZ_R0;
inline constexpr MiniBoard kZ_R3 = kZ_R1;

// J-piece
inline constexpr MiniBoard kJ_R0 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[2][1] = 1; b[2][2] = 1; b[2][3] = 1;
    return b;
}();
inline constexpr MiniBoard kJ_R1 = [] {
    MiniBoard b{};
    b[1][2] = 1; b[2][2] = 1; b[3][2] = 1; b[3][1] = 1;
    return b;
}();
inline constexpr MiniBoard kJ_R2 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[1][2] = 1; b[1][3] = 1; b[2][3] = 1;
    return b;
}();
inline constexpr MiniBoard kJ_R3 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[1][2] = 1; b[2][2] = 1; b[3][2] = 1;
    return b;
}();

// L-piece
inline constexpr MiniBoard kL_R0 = [] {
    MiniBoard b{};
    b[1][3] = 1; b[2][1] = 1; b[2][2] = 1; b[2][3] = 1;
    return b;
}();
inline constexpr MiniBoard kL_R1 = [] {
    MiniBoard b{};
    b[1][1] = 1; b[1][2] = 1; b[2][2] = 1; b[3][2] = 1;
    return b;
}();
inline constexpr MiniBoard kL_R2 = [] {
    MiniBoard b{};
    b[2][1] = 1; b[2][2] = 1; b[2][3] = 1; b[1][1] = 1;
    return b;
}();
inline constexpr MiniBoard kL_R3 = [] {
    MiniBoard b{};
    b[1][2] = 1; b[2][2] = 1; b[3][1] = 1; b[3][2] = 1;
    return b;
}();

// 取方块在某旋转状态下的 4×4 网格
inline const MiniBoard& getShape(PieceType t, Rotation r) {
    switch (t) {
        case PieceType::I:
            switch (r) {
                case Rotation::R0: return kI_R0;
                case Rotation::R1: return kI_R1;
                case Rotation::R2: return kI_R2;
                case Rotation::R3: return kI_R3;
            }
            break;
        case PieceType::O: return kO;
        case PieceType::T:
            switch (r) {
                case Rotation::R0: return kT_R0;
                case Rotation::R1: return kT_R1;
                case Rotation::R2: return kT_R2;
                case Rotation::R3: return kT_R3;
            }
            break;
        case PieceType::S:
            switch (r) {
                case Rotation::R0: return kS_R0;
                case Rotation::R1: return kS_R1;
                case Rotation::R2: return kS_R2;
                case Rotation::R3: return kS_R3;
            }
            break;
        case PieceType::Z:
            switch (r) {
                case Rotation::R0: return kZ_R0;
                case Rotation::R1: return kZ_R1;
                case Rotation::R2: return kZ_R2;
                case Rotation::R3: return kZ_R3;
            }
            break;
        case PieceType::J:
            switch (r) {
                case Rotation::R0: return kJ_R0;
                case Rotation::R1: return kJ_R1;
                case Rotation::R2: return kJ_R2;
                case Rotation::R3: return kJ_R3;
            }
            break;
        case PieceType::L:
            switch (r) {
                case Rotation::R0: return kL_R0;
                case Rotation::R1: return kL_R1;
                case Rotation::R2: return kL_R2;
                case Rotation::R3: return kL_R3;
            }
            break;
        default: break;
    }
    static const MiniBoard empty{};
    return empty;
}

// 旋转后坐标 (相对中心，4x4 网格内)
struct WallKickOffset { int dx, dy; };

// JLSTZ-piece Wall Kick (R0→R1 = "0→R", R1→R0 = "R→0" 等)
// 来源: https://harddrop.com/wiki/SRS
inline constexpr WallKickOffset kJLSTZ_Kicks[8][4] = {
    // 0→R
    { {0,0}, {-1,0}, {-1, 1}, {0,-2} },
    // R→0
    { {0,0}, { 1,0}, { 1,-1}, {0, 2} },
    // R→2
    { {0,0}, { 1,0}, { 1,-1}, {0, 2} },
    // 2→R
    { {0,0}, {-1,0}, {-1, 1}, {0,-2} },
    // 2→L
    { {0,0}, { 1,0}, { 1, 1}, {0,-2} },
    // L→2
    { {0,0}, {-1,0}, {-1,-1}, {0, 2} },
    // L→0
    { {0,0}, {-1,0}, {-1,-1}, {0, 2} },
    // 0→L
    { {0,0}, { 1,0}, { 1, 1}, {0,-2} },
};

// I-piece Wall Kick
inline constexpr WallKickOffset kI_Kicks[8][4] = {
    // 0→R
    { {0,0}, {-2,0}, { 1,0}, {-2,-1} },
    // R→0
    { {0,0}, { 2,0}, {-1,0}, { 2, 1} },
    // R→2
    { {0,0}, {-1,0}, { 2,0}, {-1, 2} },
    // 2→R
    { {0,0}, { 1,0}, {-2,0}, { 1,-2} },
    // 2→L
    { {0,0}, { 2,0}, {-1,0}, { 2, 1} },
    // L→2
    { {0,0}, {-2,0}, { 1,0}, {-2,-1} },
    // L→0
    { {0,0}, { 1,0}, {-2,0}, { 1,-2} },
    // 0→L
    { {0,0}, {-1,0}, { 2,0}, {-1, 2} },
};

// Wall Kick 索引: 0=0→R, 1=R→0, 2=R→2, 3=2→R, 4=2→L, 5=L→2, 6=L→0, 7=0→L
inline int kickIndex(Rotation from, Rotation to) {
    int f = static_cast<int>(from);
    int t = static_cast<int>(to);
    if (f == 0 && t == 1) return 0;
    if (f == 1 && t == 0) return 1;
    if (f == 1 && t == 2) return 2;
    if (f == 2 && t == 1) return 3;
    if (f == 2 && t == 3) return 4;
    if (f == 3 && t == 2) return 5;
    if (f == 3 && t == 0) return 6;
    if (f == 0 && t == 3) return 7;
    return 0;  // no rotation
}

}  // namespace tetris::srs
