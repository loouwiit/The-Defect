#pragma once

#include "bleGamepad/gamepadState.hpp"
#include <cstdint>
#include <functional>

class App;

/**
 * @brief 虚拟手柄 — 系统层统一输入管理器
 *
 * 所有输入源（本地 BLE + 远程从机）通过 inject() 注入到这里，
 * 再通过 hostMask 掩码过滤后，由 pollAndDispatch() 分发给当前 App。
 *
 * 游戏无需关心输入来自本地还是远程。
 */
class VirtualGamepad {
public:
    static VirtualGamepad& instance();

    // ── 掩码控制 ──
    // hostMask[hostId] 的 bit[playerId] = 1 表示启用该输入
    // 示例: {0x0F, 0x03, 0x00, 0x00}
    //   → 本机 P0-P3 启用，从机1的 P0-P1 启用
    void setInputMask(const uint16_t hostMask[4]);
    const uint16_t* getInputMask() const { return m_hostMask; }

    // ── 输入注入（由 BleGamepad / HostManager 调用）──
    void inject(int8_t hostId, int8_t playerId, const GamepadState& state);

    // ── 统一分发（主循环每帧调用）──
    // 遍历所有掩码启用的 (hostId, playerId)，调用 app->onGamepadInput()
    void pollAndDispatch(App* app);

    // ── 读取指定输入的最新状态（供需要轮询的游戏使用）──
    bool read(int8_t hostId, int8_t playerId, GamepadState& out) const;

    // ── 当前 App 的输入来源计数（用于判断有多少活跃输入源）──
    int getActiveSourceCount() const;

private:
    VirtualGamepad();
    ~VirtualGamepad() = default;
    VirtualGamepad(const VirtualGamepad&) = delete;
    VirtualGamepad& operator=(const VirtualGamepad&) = delete;

    static constexpr int HOST_COUNT = 4;
    static constexpr int PLAYERS_PER_HOST = 4;

    // 状态缓存: [hostId][playerId]
    GamepadState m_states[HOST_COUNT][PLAYERS_PER_HOST]{};
    bool m_dirty[HOST_COUNT][PLAYERS_PER_HOST]{};
    uint16_t m_hostMask[HOST_COUNT]{};
};
