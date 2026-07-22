#include "virtualGamepad.hpp"
#include "app/app.hpp"
#include <esp_log.h>
#include <cstring>

static constexpr char TAG[] = "VirtualGamepad";

// ============================================================
//  单例
// ============================================================

VirtualGamepad& VirtualGamepad::instance()
{
    static VirtualGamepad s_inst;
    return s_inst;
}

VirtualGamepad::VirtualGamepad()
{
    // 默认: 本机 (hostId=0) 的 P0-P3 全部启用
    m_hostMask[0] = 0x0F;
    // 其他主机默认关闭
    m_hostMask[1] = 0x00;
    m_hostMask[2] = 0x00;
    m_hostMask[3] = 0x00;

    ESP_LOGI(TAG, "initialized");
}

// ============================================================
//  掩码
// ============================================================

void VirtualGamepad::setInputMask(const uint16_t hostMask[4])
{
    for (int i = 0; i < HOST_COUNT; i++) {
        m_hostMask[i] = hostMask[i];
    }
    ESP_LOGI(TAG, "mask: 0x%04x 0x%04x 0x%04x 0x%04x",
             m_hostMask[0], m_hostMask[1], m_hostMask[2], m_hostMask[3]);
}

// ============================================================
//  注入输入
// ============================================================

void VirtualGamepad::inject(int8_t hostId, int8_t playerId, const GamepadState& state)
{
    if (hostId < 0 || hostId >= HOST_COUNT) return;
    if (playerId < 0 || playerId >= PLAYERS_PER_HOST) return;

    // 只有状态变化时才标记 dirty
    if (m_states[hostId][playerId] != state) {
        m_states[hostId][playerId] = state;
        m_dirty[hostId][playerId] = true;
    }
}

// ============================================================
//  分发输入
// ============================================================

void VirtualGamepad::pollAndDispatch(App* app)
{
    if (!app) return;

    for (int hostId = 0; hostId < HOST_COUNT; hostId++) {
        uint16_t mask = m_hostMask[hostId];
        if (mask == 0) continue;  // 该 host 全部禁用

        for (int playerId = 0; playerId < PLAYERS_PER_HOST; playerId++) {
            if (!(mask & (1u << playerId))) continue;  // 该 player 禁用
            if (!m_dirty[hostId][playerId]) continue;  // 无变化

            m_dirty[hostId][playerId] = false;
            app->onGamepadInput(hostId, playerId, m_states[hostId][playerId]);
        }
    }
}

// ============================================================
//  读取
// ============================================================

bool VirtualGamepad::read(int8_t hostId, int8_t playerId, GamepadState& out) const
{
    if (hostId < 0 || hostId >= HOST_COUNT) return false;
    if (playerId < 0 || playerId >= PLAYERS_PER_HOST) return false;
    if (!(m_hostMask[hostId] & (1u << playerId))) return false;

    out = m_states[hostId][playerId];
    return true;
}

int VirtualGamepad::getActiveSourceCount() const
{
    int count = 0;
    for (int h = 0; h < HOST_COUNT; h++) {
        uint16_t mask = m_hostMask[h];
        for (int p = 0; p < PLAYERS_PER_HOST; p++) {
            if (mask & (1u << p)) count++;
        }
    }
    return count;
}
