#include "virtualGamepad.hpp"
#include "app/app.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

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

	m_thread = Thread{ dispatchTask, "vGamepadDisp", this, Thread::Priority::High, 8192 };
	ESP_LOGI(TAG, "dispatch task started (60fps)");
}

void VirtualGamepad::inject(int8_t hostId, int8_t playerId, const GamepadState& state)
{
	if (hostId < 0 || hostId >= 4 || playerId < 0 || playerId >= 4)
		return;
	m_states[hostId][playerId] = state;
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
