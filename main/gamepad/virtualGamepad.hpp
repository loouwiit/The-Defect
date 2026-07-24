#pragma once

#include <cstdint>
#include "gamepad/gamepadState.hpp"
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

	/// 启动分发 Task
	void start(Display* display);

	/// 注入一帧手柄状态
	void inject(int8_t hostId, int8_t playerId, const GamepadState& state);

	// ── 掩码控制 ──

	/// 设置某主机的玩家掩码：hostMask[hostId] 的 bit[playerId] = 1 表示启用
	void setHostMask(int8_t hostId, uint8_t playerMask);

	/// 全量设置 4 个主机的掩码
	void setAllMasks(const uint8_t masks[4]);

	/// 复位某输入源到中立状态
	void resetPlayer(int8_t hostId, int8_t playerId);

	/// 查询某输入源的最新状态
	const GamepadState& getState(int8_t hostId, int8_t playerId) const;

	// ── 远程转发（从机模式，UDP）──

	/// 启动转发线程（由 HostManager 注册成功后调用）
	void startForwarder();

	/// 停止转发线程（断开连接时调用）
	void stopForwarder();

private:
	VirtualGamepad() = default;
	~VirtualGamepad() = default;
	VirtualGamepad(const VirtualGamepad&) = delete;

	/// 打包 hostId + GamepadState 到 11 字节缓冲区
	static void packFrame(uint8_t buf[11], int8_t hostId, int8_t playerId, const GamepadState& state);

	/// 独立转发线程：将本机输入异步发送到远程主机
	static void dispatchTask(void* arg);
	static void forwarderTask(void* arg);
	void forwarderLoop();

	Display* m_display = nullptr;

	Thread m_thread{};

	// [hostId][playerId] 最新状态
	GamepadState m_states[4][4]{};

	// hostMask[hostId] 的 bit[playerId] = 1 表示启用该输入源
	// 默认：仅本机 4 个手柄全开
	uint8_t m_hostMask[4] = { 0x0F, 0x07, 0x07, 0x07 };

	// ── 远程转发（独立线程，不阻塞 dispatch）──
	Thread m_forwardThread{};
	bool m_forwarderRunning = false;

	// ── 上次转发的状态（用于去重）──
	GamepadState m_lastForwarded[4]{};
};
