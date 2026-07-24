#include "virtualGamepad.hpp"
#include "host/hostManager.hpp"
#include "host/udpInput.hpp"
#include "websocket/wsClient.hpp"
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

void VirtualGamepad::setRemoteForwarder(WsClient* client)
{
	m_remoteForwarder = client;
	if (client)
	{
		m_forwarderRunning = true;
		m_forwardThread = Thread{ forwarderTask, "vGamepadFwd", this, Thread::Priority::Normal, 4096 };
		ESP_LOGI(TAG, "remote forwarder set, forwarder thread started");
	}
	else
	{
		m_forwarderRunning = false;
		ESP_LOGI(TAG, "remote forwarder cleared");
	}
}

void VirtualGamepad::clearRemoteForwarder()
{
	m_forwarderRunning = false;
	m_remoteForwarder = nullptr;
	ESP_LOGI(TAG, "remote forwarder cleared");
}

void VirtualGamepad::packFrame(uint8_t buf[11], int8_t hostId, int8_t playerId, const GamepadState& state)
{
	buf[0] = (uint8_t)hostId;
	buf[1] = (uint8_t)playerId;
	buf[2] = (uint8_t)(state.buttons & 0xFF);
	buf[3] = (uint8_t)(state.buttons >> 8);
	buf[4] = state.lx;
	buf[5] = state.ly;
	buf[6] = state.rx;
	buf[7] = state.ry;
	buf[8] = state.lt;
	buf[9] = state.rt;
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

		// 保持 ~60fps
		TickType_t elapsed = xTaskGetTickCount() - startTick;
		TickType_t delay = pdMS_TO_TICKS(16);
		if (elapsed < delay)
			vTaskDelay(delay - elapsed);
		else
			vTaskDelay(1);
	}
}

// ════════════════════════════════════════════════════════════════
//  Forwarder Task — 独立线程异步转发本机输入到远程主机
//  Normal 优先级，可安全阻塞，不影响 dispatch 的 60fps 本地分发
// ════════════════════════════════════════════════════════════════

void VirtualGamepad::forwarderTask(void* arg)
{
	static_cast<VirtualGamepad*>(arg)->forwarderLoop();
	vTaskDelete(nullptr);
}

void VirtualGamepad::forwarderLoop()
{
	while (m_forwarderRunning)
	{
		const char* hostIp = HostManager::instance().getRemoteIp();
		if (hostIp && hostIp[0])
		{
			int8_t slot = HostManager::instance().getMySlot();
			if (slot >= 0)
			{
				for (int p = 0; p < 4; p++)
				{
					const auto& state = m_states[0][p];
					auto& last = m_lastForwarded[p];

					// 去重：仅状态变化时发送
					if (last.buttons != state.buttons ||
						last.lx != state.lx || last.ly != state.ly ||
						last.dpad != state.dpad)
					{
						last = state;
						uint8_t buf[11];
						packFrame(buf, slot, p, state);
						udpInputSend(hostIp, 8082, buf, 11);
						ESP_LOGI(TAG, "forwarded H%d P%d: btn=0x%04X lx=%d ly=%d dpad=%d",
							slot, p, state.buttons, state.lx, state.ly, state.dpad);
					}
				}
			}
		}

		// ~60fps 轮询，不忙等
		vTaskDelay(pdMS_TO_TICKS(16));
	}
}
