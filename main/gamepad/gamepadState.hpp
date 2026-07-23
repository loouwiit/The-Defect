#pragma once

#include <cstdint>

static constexpr uint8_t MaxPlayers = 4;

// 按钮位定义 — 与 C6 手柄固件 HID 报告描述符对齐
enum GamepadButton : uint16_t {
    BTN_A      = 1 << 0,   // GPIO0 — A (SOUTH)
    BTN_B      = 1 << 1,   // 预留 — B (EAST)
    BTN_X      = 1 << 3,   // 预留 — X (NORTH)
    BTN_Y      = 1 << 4,   // 预留 — Y (WEST)
    BTN_TL     = 1 << 6,   // 预留 — LB
    BTN_TR     = 1 << 7,   // 预留 — RB
    BTN_TL2    = 1 << 8,   // 预留 — LT 按到底
    BTN_TR2    = 1 << 9,   // 预留 — RT 按到底
    BTN_SELECT = 1 << 10,  // 预留
    BTN_START  = 1 << 11,  // 预留
    BTN_L3     = 1 << 13,  // GPIO6 — 左摇杆按下 (THUMBL)
    BTN_R3     = 1 << 14,  // 预留 — 右摇杆按下 (THUMBR)
};

struct GamepadState {
    uint16_t buttons{};       // 按钮位图 (使用 GamepadButton 枚举访问)
    uint8_t lx{128}, ly{128}; // 左摇杆 X/Y (0~255, 中心 128)
    uint8_t rx{128}, ry{128}; // 右摇杆 X/Y (0~255, 中心 128)
    uint8_t lt{0}, rt{0};     // 左/右扳机 (0~255)
    uint8_t dpad{15};         // D-pad (0~7=方向, 15=松开)

    bool isPressed(GamepadButton btn) const { return (buttons & static_cast<uint16_t>(btn)) != 0; }
    bool operator==(const GamepadState& o) const {
        return buttons == o.buttons && lx == o.lx && ly == o.ly
            && rx == o.rx && ry == o.ry && lt == o.lt && rt == o.rt
            && dpad == o.dpad;
    }
    bool operator!=(const GamepadState& o) const { return !(*this == o); }
};

// 队列消息 — 携带设备标识
struct GamepadInputEvent {
    uint8_t playerId;
    GamepadState state;
};

// 扫描到的设备信息
struct ScanDevice {
    uint8_t bda[6];
    uint8_t addrType;
    char name[32];
    int8_t rssi;
    uint16_t appearance;
};

// 已连接设备上下文
struct DeviceContext {
    uint16_t connHandle{};       // NimBLE 连接句柄
    uint8_t playerId{};
    GamepadState state;
    bool connected{false};
    char name[32]{};
    uint8_t bda[6]{};
    uint8_t batteryLevel{ 255 };   // 0~100, 255=未知
    uint16_t batteryHandle{};    // Battery Level 特征值句柄
    uint16_t batteryCccHandle{}; // Battery CCCD 句柄
};
