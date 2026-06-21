#pragma once

#include <cstdint>

static constexpr uint8_t MAX_PLAYERS = 4;

struct GamepadState {
    uint16_t buttons{};       // bit0=A, bit1=B, bit2=X, bit3=Y, ...
    uint8_t lx{128}, ly{128}; // 左摇杆 X/Y (0~255, 中心 128)
    uint8_t rx{128}, ry{128}; // 右摇杆 X/Y (0~255, 中心 128)
    uint8_t lt{0}, rt{0};     // 左/右扳机 (0~255)
    uint8_t dpad{15};         // D-pad (0~7=方向, 15=松开)

    bool isPressed(uint8_t bit) const { return (buttons & (1u << bit)) != 0; }
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
    void* dev{};           // esp_hidh_dev_t*
    uint8_t playerId{};
    GamepadState state;
    bool connected{false};
    char name[32]{};
    uint8_t bda[6]{};
};
