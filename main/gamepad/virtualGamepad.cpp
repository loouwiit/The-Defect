#include "virtualGamepad.hpp"
#include "host/hostManager.hpp"
#include "host/udpInput.hpp"
#include "app/app.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"
#include "battery/batteryMonitor.hpp"
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

void VirtualGamepad::startForwarder()
{
	m_forwarderRunning = true;
	m_forwardThread = Thread{ forwarderTask, "vGamepadFwd", this, Thread::Priority::RealTime, 4096 };
	ESP_LOGI(TAG, "forwarder thread started");
}

void VirtualGamepad::stopForwarder()
{
	m_forwarderRunning = false;
	// ⚠ 必须等待旧线程真正退出（forwarderTask 收尾执行 m_forwardThread = {} 已清空句柄）
	//   再返回：否则旧线程收尾会把新 startForwarder() 写入的句柄从成员换走并 vTaskDelete，
	//   新转发线程被误杀（从机断线重连场景，m_forwardThread 永久为空）。
	while (m_forwardThread.isRunning())
		vTaskDelay(10);
	ESP_LOGI(TAG, "forwarder stopped");
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
			// AI Overlay 或低电量弹窗显示时独占输入，不向 App 分发
			if (!AiAgentOverlay::instance().isVisible() &&
				!BatteryMonitor::instance().isAlertShown()) {
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
	auto& self = *static_cast<VirtualGamepad*>(arg);
	self.forwarderLoop();
	self.m_forwardThread = {};
	while (true)
		vTaskDelay(1000);
}

void VirtualGamepad::forwarderLoop()
{
	int sock = -1;
	bool needCreateSocket = true;

	while (m_forwarderRunning)
	{
		const char* hostIp = HostManager::instance().getRemoteIp();
		if (hostIp && hostIp[0])
		{
			// 需要时创建持久 socket（只创建一次，后续复用）
			if (needCreateSocket)
			{
				if (sock >= 0) udpInputCloseSocket(sock);
				sock = udpInputCreateSocket(hostIp, 8082);
				needCreateSocket = false;
				if (sock < 0)
					ESP_LOGW(TAG, "create socket failed, will retry");
			}

			if (sock >= 0)
			{
				int8_t slot = HostManager::instance().getMySlot();
				if (slot >= 0)
				{
					// m_hostMask[0] 在从机侧是反码（主机想要的→0），取反得主机期望的转发 mask
					uint8_t forwardMask = (~m_hostMask[0]) & 0x0F;
					for (int p = 0; p < 4; p++)
					{
						if (!(forwardMask & (1 << p))) continue;
						const auto& state = m_states[0][p];
						auto& last = m_lastForwarded[p];

						if (last.buttons != state.buttons ||
							last.lx != state.lx || last.ly != state.ly ||
							last.rx != state.rx || last.ry != state.ry ||
							last.lt != state.lt || last.rt != state.rt ||
							last.dpad != state.dpad)
						{
							last = state;
							uint8_t buf[11];
							packFrame(buf, slot, p, state);
							udpInputSend(sock, buf, 11);
						}
					}
				}
			}
		}
		else
		{
			// 没有远程主机，关闭 socket
			needCreateSocket = true;
			if (sock >= 0)
			{
				udpInputCloseSocket(sock);
				sock = -1;
			}
		}

		vTaskDelay(pdMS_TO_TICKS(33));
	}

	if (sock >= 0)
		udpInputCloseSocket(sock);
}
