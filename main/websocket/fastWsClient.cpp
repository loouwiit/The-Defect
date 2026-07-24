/**
 * @file fastWsClient.cpp
 * @brief 快速 WebSocket 客户端 — 基于裸 TCP socket
 */

#include "fastWsClient.hpp"
#include <esp_log.h>
#include <esp_random.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <errno.h>

static constexpr char TAG[] = "FastWs";

// RFC 6455 固定 GUID
static constexpr char WS_GUID[] = "258EAFA5-E914-47DA-95CA-5AB9DC85B175";

// 简单的 Base64 编码（仅用于 Sec-WebSocket-Key）
static void base64Encode(const uint8_t* in, size_t inLen, char* out, size_t outLen)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i = 0, o = 0;
	while (i < inLen && o + 4 <= outLen) {
		uint32_t v = ((uint32_t)in[i]) << 16;
		if (i + 1 < inLen) v |= ((uint32_t)in[i + 1]) << 8;
		if (i + 2 < inLen) v |= in[i + 2];
		out[o++] = b64[(v >> 18) & 0x3F];
		out[o++] = b64[(v >> 12) & 0x3F];
		out[o++] = (i + 1 < inLen) ? b64[(v >> 6) & 0x3F] : '=';
		out[o++] = (i + 2 < inLen) ? b64[v & 0x3F] : '=';
		i += 3;
	}
	if (o < outLen) out[o] = '\0';
}

// ── 构造 / 析构 ──

FastWsClient::FastWsClient() = default;

FastWsClient::~FastWsClient()
{
	disconnect();
}

// ── 核心读写 ──

static bool sendAll(int fd, const void* data, size_t len)
{
	auto* buf = static_cast<const char*>(data);
	while (len > 0) {
		ssize_t n = send(fd, buf, len, MSG_DONTWAIT);
		if (n <= 0) {
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
				return true;  // 缓冲区满，静默丢弃，下帧覆盖
			return false;
		}
		buf += n;
		len -= (size_t)n;
	}
	return true;
}

static bool recvLine(int fd, char* buf, size_t maxLen)
{
	size_t i = 0;
	while (i < maxLen - 1) {
		char c;
		ssize_t n = recv(fd, &c, 1, 0);
		if (n <= 0) return false;
		if (c == '\n') break;
		if (c != '\r') buf[i++] = c;
	}
	buf[i] = '\0';
	return true;
}

// ── WebSocket 帧组帧 ──

/// 构造客户端→服务器的二进制 WebSocket 帧（含 masking）
/// 返回帧总长度（写入 out 缓冲区）
static size_t buildWsFrame(uint8_t* out, size_t outMax,
	const uint8_t* payload, size_t payloadLen, bool isText)
{
	if (outMax < payloadLen + 6) return 0;

	out[0] = 0x80 | (isText ? 0x01 : 0x02);  // FIN + opcode

	// 掩码 key（固定值，局域网内安全无影响）
	static const uint8_t maskKey[4] = { 0x12, 0x34, 0x56, 0x78 };

	size_t pos;
	if (payloadLen < 126) {
		out[1] = 0x80 | (uint8_t)payloadLen;
		pos = 2;
	} else if (payloadLen < 65536) {
		out[1] = 0x80 | 126;
		out[2] = (uint8_t)(payloadLen >> 8);
		out[3] = (uint8_t)(payloadLen);
		pos = 4;
	} else {
		out[1] = 0x80 | 127;
		uint64_t n = payloadLen;
		for (int i = 7; i >= 0; i--) { out[2 + i] = (uint8_t)(n & 0xFF); n >>= 8; }
		pos = 10;
	}

	// mask key
	for (int i = 0; i < 4; i++)
		out[pos++] = maskKey[i];

	// masked payload
	for (size_t i = 0; i < payloadLen; i++)
		out[pos++] = payload[i] ^ maskKey[i % 4];

	return pos;
}

// ════════════════════════════════════════════════════════════════
//  connect — TCP + HTTP Upgrade
// ════════════════════════════════════════════════════════════════

bool FastWsClient::connect(const char* host, uint16_t port, const char* uri)
{
	if (m_fd >= 0) {
		ESP_LOGW(TAG, "already connected");
		return false;
	}

	// ── DNS 解析 ──
	struct addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	char portStr[8];
	snprintf(portStr, sizeof(portStr), "%u", port);

	struct addrinfo* res = nullptr;
	int err = getaddrinfo(host, portStr, &hints, &res);
	if (err != 0 || !res) {
		ESP_LOGE(TAG, "DNS lookup failed for %s: %d", host, err);
		return false;
	}

	// ── TCP 连接（先阻塞，用于 HTTP 升级握手）──
	m_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (m_fd < 0) {
		ESP_LOGE(TAG, "socket() failed");
		freeaddrinfo(res);
		return false;
	}

	// TCP_NODELAY — 禁用 Nagle，确保小包实时发出
	int flag = 1;
	setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
	setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	if (::connect(m_fd, res->ai_addr, res->ai_addrlen) != 0) {
		ESP_LOGE(TAG, "connect to %s:%u failed", host, port);
		::close(m_fd);
		m_fd = -1;
		freeaddrinfo(res);
		return false;
	}
	freeaddrinfo(res);

	ESP_LOGI(TAG, "TCP connected to %s:%u", host, port);

	// ── HTTP Upgrade 请求 ──
	// Sec-WebSocket-Key: 16 字节随机 → Base64
	uint8_t keyRaw[16];
	for (int i = 0; i < 16; i++) keyRaw[i] = (uint8_t)(esp_random() & 0xFF);
	char keyB64[32];
	base64Encode(keyRaw, 16, keyB64, sizeof(keyB64));

	char request[512];
	int reqLen = snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		"Host: %s:%u\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n",
		uri, host, port, keyB64);

	if (reqLen <= 0 || (size_t)reqLen >= sizeof(request)) {
		ESP_LOGE(TAG, "request too long");
		::close(m_fd);
		m_fd = -1;
		return false;
	}

	// HTTP 升���请求（用阻塞发送，确保完整发送）
	{
		auto* p = request;
		size_t remain = (size_t)reqLen;
		while (remain > 0) {
			ssize_t n = send(m_fd, p, remain, 0);
			if (n <= 0) {
				ESP_LOGE(TAG, "send upgrade request failed");
				::close(m_fd);
				m_fd = -1;
				return false;
			}
			p += n;
			remain -= (size_t)n;
		}
	}

	// ── 读取响应 ──
	char line[128];
	if (!recvLine(m_fd, line, sizeof(line))) {
		ESP_LOGE(TAG, "no response");
		::close(m_fd);
		m_fd = -1;
		return false;
	}

	// 检查 HTTP 101
	int httpMajor = 0, httpMinor = 0, httpCode = 0;
	if (sscanf(line, "HTTP/%d.%d %d", &httpMajor, &httpMinor, &httpCode) < 3 || httpCode != 101) {
		ESP_LOGE(TAG, "upgrade failed: %s", line);
		::close(m_fd);
		m_fd = -1;
		return false;
	}

	// 跳过剩余响应头
	while (true) {
		if (!recvLine(m_fd, line, sizeof(line))) {
			::close(m_fd);
			m_fd = -1;
			return false;
		}
		if (line[0] == '\0') break;  // 空行 = 响应头结束
	}

	ESP_LOGI(TAG, "WebSocket upgrade OK on %s:%u%s", host, port, uri);

	// 切换为非阻塞模式（数据帧发送不能被阻塞）
	int nonblock = 1;
	ioctl(m_fd, FIONBIO, &nonblock);

	if (m_onConnected) m_onConnected();
	return true;
}

// ════════════════════════════════════════════════════════════════
//  断开
// ════════════════════════════════════════════════════════════════

void FastWsClient::disconnect()
{
	if (m_fd < 0) return;
	::close(m_fd);
	m_fd = -1;
	ESP_LOGI(TAG, "disconnected");
	if (m_onDisconnected) m_onDisconnected();
}

bool FastWsClient::isConnected() const
{
	return m_fd >= 0;
}

// ════════════════════════════════════════════════════════════════
//  发送
// ════════════════════════════════════════════════════════════════

bool FastWsClient::sendBinary(const uint8_t* data, size_t len)
{
	if (m_fd < 0) return false;

	uint8_t frame[512];  // 够装单帧
	size_t frameLen = buildWsFrame(frame, sizeof(frame), data, len, false);
	if (frameLen == 0) {
		ESP_LOGE(TAG, "frame too large (%zu)", len);
		return false;
	}

	if (!sendAll(m_fd, frame, frameLen)) {
		ESP_LOGW(TAG, "send failed");
		disconnect();
		return false;
	}
	return true;
}

bool FastWsClient::sendText(const char* data, size_t len)
{
	if (m_fd < 0) return false;

	uint8_t frame[512];
	size_t frameLen = buildWsFrame(frame, sizeof(frame),
		reinterpret_cast<const uint8_t*>(data), len, true);
	if (frameLen == 0) {
		ESP_LOGE(TAG, "frame too large (%zu)", len);
		return false;
	}

	if (!sendAll(m_fd, frame, frameLen)) {
		ESP_LOGW(TAG, "send failed");
		disconnect();
		return false;
	}
	return true;
}
