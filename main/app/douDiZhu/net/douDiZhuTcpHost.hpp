#pragma once

#include <cstdint>
#include <functional>
#include "mutex/mutex.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief 斗地主主机端 — 主动连接各从机（反客为主，同 tetrisTcpHostConnectToClient）
 *
 * 职责：
 *   - 按 seat(1/2) 维护与从机的 TCP 连接（各配 recv Task）
 *   - sendToSeat：向指定座位发送（**隐私隔离**：只向该座位发它自己的手牌）
 *   - broadcast：向所有从机广播公开桌面信息
 *   - 回调在 recv Task 上下文触发，App 侧应投递到事件队列
 */
class DouDizhuTcpHost
{
public:
	static constexpr int MAX_CLIENTS = 2;   // seat 1, seat 2

	struct Callbacks {
		std::function<void(int seat, int value)> onBid;              // value 0=不叫
		std::function<void(int seat, const uint8_t* cards, int count)> onPlay;
		std::function<void(int seat)> onPass;
		std::function<void(int seat)> onRematch;
		std::function<void(int seat)> onDisconnect;
	};

	DouDizhuTcpHost() = default;
	~DouDizhuTcpHost();

	void start(const Callbacks& cb);
	void stop();

	/// 主机主动连接从机；seat 为 1 或 2；成功返回 true
	bool connectToClient(const char* ip, uint16_t port, int seat);

	void sendToSeat(int seat, const char* json);
	void broadcast(const char* json);

	bool isSeatConnected(int seat) const;

private:
	struct Conn {
		int sock = -1;
		int seat = -1;
		bool active = false;
		TaskHandle_t recvTask = nullptr;
		char lineBuf[2048]{};
		size_t lineLen = 0;
	};

	Conn m_conns[MAX_CLIENTS]{};
	Callbacks m_cb;
	Mutex m_mutex;
	volatile bool m_started = false;

	struct TaskArg { DouDizhuTcpHost* self; Conn* conn; };

	static void recvTaskFn(void* arg);
	void processLine(Conn& c, const char* line);
	void closeConn(Conn& c);
};
