#include "virtualGamepad.hpp"
#include "host/hostManager.hpp"
#include "websocket/fastWsClient.hpp"
#include "app/app.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstring>

static constexpr char TAG[] = "VirtualGamepad";

// ════════════════════════════════════════════════════════════════
//  单例
// ════════════════════════════════════════════════════════════════

VirtualGamepad& VirtualGamepad::instance()
{
	static VirtualGamepad s_instance{};
	return s_instance;
}

// ════════════════════════════════════════════════════════════════
//  start / inject
// ════════════════════════════════════════════════════════════════

void VirtualGamepad::start(Display* display)
{
	m_display = display;

	m_thread = Thread{ dispatchTask, "vGamepadDisp", this, Thread::Priority::Veryhigh, 32768 };
	ESP_LOGI(TAG, "dispatch task started");
}

void VirtualGamepad::inject(int8_t hostId, int8_t playerId, const GamepadState& state)
{
	if (hostId < 0 || hostId >= 4 || playerId < 0 || playerId >= 4)
		return;

	m_states[hostId][playerId] = state;

	// 自动启用有输入的 hostId（方便首次连接时开对应 slot）
	if (!(m_hostMask[hostId] & (1 << playerId))) {
		m_hostMask[hostId] |= (1 << playerId);
		ESP_LOGI(TAG, "auto-enabled H%d P%d (mask 0x%02X)", hostId, playerId, m_hostMask[hostId]);
	}
}

// ════════════════════════════════════════════════════════════════
//  掩码
// ════════════════════════════════════════════════════════════════

void VirtualGamepad::setHostMask(int8_t hostId, uint8_t playerMask)
{
	if (hostId < 0 || hostId >= 4) return;
	m_hostMask[hostId] = playerMask;
}

void VirtualGamepad::setAllMasks(const uint8_t masks[4])
{
	for (int i = 0; i < 4; i++)
		m_hostMask[i] = masks[i];
}

void VirtualGamepad::resetPlayer(int8_t hostId, int8_t playerId)
{
	if (hostId < 0 || hostId >= 4 || playerId < 0 || playerId >= 4)
		return;
	m_states[hostId][playerId] = GamepadState{};
	ESP_LOGI(TAG, "reset H%d P%d", hostId, playerId);
}

const GamepadState& VirtualGamepad::getState(int8_t hostId, int8_t playerId) const
{
	static GamepadState s_empty;
	if (hostId < 0 || hostId >= 4 || playerId < 0 || playerId >= 4)
		return s_empty;
	return m_states[hostId][playerId];
}

// ════════════════════════════════════════════════════════════════
//  远程转发
// ════════════════════════════════════════════════════════════════

void VirtualGamepad::setRemoteForwarder(FastWsClient* client)
{
	m_remoteForwarder = client;
	if (client)
		ESP_LOGI(TAG, "remote forwarder set");
	else
		ESP_LOGI(TAG, "remote forwarder cleared");
}

void VirtualGamepad::clearRemoteForwarder()
{
	m_remoteForwarder = nullptr;
	ESP_LOGI(TAG, "remote forwarder cleared");
}

void VirtualGamepad::packBatchedFrame(uint8_t buf[42], int8_t hostId,
	const GamepadState states[4], const GamepadState lastForwarded[4],
	uint8_t& outPlayerMask)
{
	buf[0] = (uint8_t)hostId;
	uint8_t mask = 0;
	for (int p = 0; p < 4; p++) {
		const auto& s = states[p];
		const auto& l = lastForwarded[p];
		if (s.buttons == l.buttons && s.lx == l.lx && s.ly == l.ly && s.dpad == l.dpad)
			continue;
		mask |= (1 << p);
		int off = 2 + p * 10;
		buf[off + 0] = (uint8_t)p;  // localPlayer
		buf[off + 1] = (uint8_t)(s.buttons & 0xFF);
		buf[off + 2] = (uint8_t)(s.buttons >> 8);
		buf[off + 3] = s.lx;
		buf[off + 4] = s.ly;
		buf[off + 5] = s.rx;
		buf[off + 6] = s.ry;
		buf[off + 7] = s.lt;
		buf[off + 8] = s.rt;
		buf[off + 9] = s.dpad;
	}
	buf[1] = mask;
	outPlayerMask = mask;
}

void VirtualGamepad::packFrame(uint8_t buf[11], int8_t hostId, int8_t playerId, const GamepadState& state)
{
	buf[0]  = (uint8_t)hostId;
	buf[1]  = (uint8_t)playerId;
	buf[2]  = (uint8_t)(state.buttons & 0xFF);
	buf[3]  = (uint8_t)(state.buttons >> 8);
	buf[4]  = state.lx;
	buf[5]  = state.ly;
	buf[6]  = state.rx;
	buf[7]  = state.ry;
	buf[8]  = state.lt;
	buf[9]  = state.rt;
	buf[10] = state.dpad;
}

// ════════════════════════════════════════════════════════════════
//  Dispatch Task — 60fps 全量分发
// ════════════════════════════════════════════════════════════════

void VirtualGamepad::dispatchTask(void* arg)
{
	auto& self = *static_cast<VirtualGamepad*>(arg);

	while (true) {
		auto startTick = xTaskGetTickCount();

		if (self.m_display) {
			// AI Overlay 显示时独占输入，不向 App 分发
			if (!AiAgentOverlay::instance().isVisible()) {
				auto* app = self.m_display->getActiveApp();
				if (app) {
					for (int h = 0; h < 4; h++) {
						for (int p = 0; p < 4; p++) {
							if ((self.m_hostMask[h] >> p) & 1) {
								app->onGamepadInput(h, p, self.m_states[h][p]);
							}
						}
					}
				}
			}
		}

		// 从机模式：60fps 统一转发本机 (hostId=0) 变化到远程主机
		if (self.m_remoteForwarder && self.m_remoteForwarder->isConnected()) {
			uint8_t buf[42];
			uint8_t playerMask = 0;
			self.packBatchedFrame(buf, HostManager::instance().getMySlot(),
				self.m_states[0], self.m_lastForwarded, playerMask);
			if (playerMask) {
				ESP_LOGI(TAG, "forwarding to remote: playerMask=0x%02X", playerMask);
				self.m_remoteForwarder->sendBinary(buf, 42);
				// 更新 lastForwarded
				for (int p = 0; p < 4; p++) {
					if (playerMask & (1 << p))
						self.m_lastForwarded[p] = self.m_states[0][p];
				}
			}
		}

		// 保持 ~60fps
		TickType_t elapsed = xTaskGetTickCount() - startTick;
		TickType_t delay = pdMS_TO_TICKS(16);
		if (elapsed < delay)
			vTaskDelay(delay - elapsed);
		else
			vTaskDelay(1);
	}
}
