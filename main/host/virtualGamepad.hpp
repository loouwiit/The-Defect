#pragma once

#include <cstdint>
#include "bleGamepad/gamepadState.hpp"
#include "display/display.hpp"
#include "task/task.hpp"

/**
 * @brief 统一手柄输入抽象层
 *
 * 系统层单例，汇集所有手柄输入源（本地 BLE + 远程客户端），
 * 在专用 Dispatch Task 中以固定 60fps 全量分发到活跃 App。
 *
 * 使用方式：
 *   - BleGamepad:     VirtualGamepad::instance().inject(0, playerId, state);
 *   - WS 远程输入:    VirtualGamepad::instance().inject(hostId, playerId, state);
 *   - start(display)  在 BleGamepad::start() 或 main.cpp 中调用
 */
class VirtualGamepad {
public:
	static VirtualGamepad& instance();

	/// 启动分发 Task（固定 60fps 独立线程）
	void start(Display* display);

	/// 注入一帧手柄状态（轻量，仅 struct copy，可在任何 Task 调用）
	void inject(int8_t hostId, int8_t playerId, const GamepadState& state);

	// ── 掩码控制 ──

	/// 设置某主机的玩家掩码：hostMask[hostId] 的 bit[playerId] = 1 表示启用
	void setHostMask(int8_t hostId, uint8_t playerMask);

	/// 全量设置 4 个主机的掩码
	void setAllMasks(const uint8_t masks[4]);

	/// 复位某输入源到中立状态（断开连接时调用）
	void resetPlayer(int8_t hostId, int8_t playerId);

	/// 查询某输入源的最新状态
	const GamepadState& getState(int8_t hostId, int8_t playerId) const;

private:
	VirtualGamepad() = default;
	~VirtualGamepad() = default;
	VirtualGamepad(const VirtualGamepad&) = delete;

	static void dispatchTask(void* arg);

	Display* m_display = nullptr;

	Thread m_thread{};

	// [hostId][playerId] 最新状态
	GamepadState m_states[4][4]{};

	// hostMask[hostId] 的 bit[playerId] = 1 表示启用该输入源
	// 默认：仅本机 4 个手柄全开
	uint8_t m_hostMask[4] = { 0x0F, 0, 0, 0 };
};
