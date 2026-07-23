#pragma once

#include <cstdint>

/**
 * @brief 远程客户端 fd → hostId 映射表
 *
 * 主机侧使用，记录每个已连接的远程客户端 WebSocket fd 对应的 hostId(1-3)。
 * 供 /ws/input handler 在收到输入帧时查询来源。
 *
 * 线程安全：所有方法在 WS Task 上下文中调用，无并发冲突。
 */
class InputHost {
public:
	static InputHost& instance();

	/// 注册一个远程客户端，返回分配的 hostId (1-3)，-1 表示已满
	int registerClient(int fd, const char* hostname);

	/// 注销（断开连接时调用）
	void unregisterClient(int fd);

	/// 根据 fd 查询 hostId，-1 表示未找到
	int getHostIdByFd(int fd) const;

	/// 判断是否有空闲槽位
	bool hasFreeSlot() const;

private:
	InputHost() = default;

	struct Slot {
		int fd = -1;
		char hostname[64]{};
		bool occupied = false;
	};

	// hostId 1-3 对应 slots[0]-slots[2]
	static constexpr int MAX_SLOTS = 3;
	Slot m_slots[MAX_SLOTS]{};
};
