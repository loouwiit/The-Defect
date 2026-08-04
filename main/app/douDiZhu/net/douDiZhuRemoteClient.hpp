#pragma once

#include <cstdint>
#include <functional>
#include "mutex/mutex.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../douDiZhuLogic.hpp"
#include "douDiZhuProtocol.hpp"

/**
 * @brief 斗地主从机客户端 — 通过 TCP 连接远程主机（由主机发起连接）
 *
 * 从机本地监听端口 8086，等待主机 accept 后：
 *   - 接收 welcome / deal / 公开桌面消息，解析进 DouDizhuClientView（线程安全）
 *   - 发送叫分 / 出牌 / 不出 / 再来一局
 */
class DouDizhuRemoteClient
{
public:
	DouDizhuRemoteClient();
	~DouDizhuRemoteClient();

	/// 在本地监听端口等待主机连接（阻塞 accept）
	bool listen(int port = DouDizhuMsg::CLIENT_PORT);
	void disconnect();
	bool isConnected() const { return m_sock >= 0; }

	int  getSeat() const;
	bool hasNewState() const;
	void consumeNewState();
	DouDizhuClientView getView() const;

	void setOnDisconnected(std::function<void()> cb) { m_onDisconnected = std::move(cb); }

	// ── 发送 ──
	void sendBid(int value);
	void sendPlay(const uint8_t* cards, int count);
	void sendPass();
	void sendRematch();

private:
	int  m_sock = -1;
	TaskHandle_t m_recvTask = nullptr;
	volatile bool m_running = false;

	mutable Mutex m_mutex;
	DouDizhuClientView m_view;
	bool m_hasNew = false;

	std::function<void()> m_onDisconnected;

	static constexpr size_t LINE_BUF_SIZE = 2048;
	char m_lineBuf[LINE_BUF_SIZE]{};
	size_t m_lineLen = 0;

	static void recvTaskFn(void* arg);
	void processRecvData(const char* data, size_t len);
	void processLine(const char* line);
};
