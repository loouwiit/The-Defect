#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>

/**
 * @brief 快速 WebSocket 客户端 — 基于裸 TCP socket
 *
 * 专为低延迟二进制帧优化（如 /ws/input 手柄透传）。
 * 使用裸 socket + 手动 WS 组帧，直接控制 TCP_NODELAY，
 * 避免 esp_websocket_client 的内部队列缓冲导致的分批发送问题。
 *
 * 注意：不支持自动重连、Ping/Pong 等高级功能。
 * 适合发送密集型场景，管理类消息请继续使用 WsClient(esp_websocket_client)。
 */
class FastWsClient {
public:
	FastWsClient();
	~FastWsClient();

	FastWsClient(const FastWsClient&) = delete;
	FastWsClient& operator=(const FastWsClient&) = delete;

	/// 连接远程 WebSocket 服务器
	bool connect(const char* host, uint16_t port, const char* uri);

	/// 断开连接
	void disconnect();

	/// 是否已连接
	bool isConnected() const;

	/// 发送二进制帧（实时发送，不缓冲）
	bool sendBinary(const uint8_t* data, size_t len);

	/// 发送文本帧
	bool sendText(const char* data, size_t len);

	// ── 事件回调 ──

	void setOnConnected(std::function<void()> cb) { m_onConnected = std::move(cb); }
	void setOnDisconnected(std::function<void()> cb) { m_onDisconnected = std::move(cb); }
	void setOnMessage(std::function<void(const char* data, size_t len)> cb) { m_onMessage = std::move(cb); }

private:
	int m_fd = -1;
	std::function<void()> m_onConnected;
	std::function<void()> m_onDisconnected;
	std::function<void(const char* data, size_t len)> m_onMessage;
};
