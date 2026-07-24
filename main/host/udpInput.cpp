/**
 * @file udpInput.cpp
 * @brief UDP 游戏手柄输入 — 替代 WebSocket /ws/input
 *
 * 发送方：udpInputSend() 直发 11 字节 UDP datagram
 * 接收方：udpInputStart() 启动后台 Task 监听端口 8082
 *
 * 为什么用 UDP 而非 WebSocket：
 *   - 手柄输入每帧 11 字节，对延迟敏感
 *   - UDP 每个 datagram 原子到达，无 Nagle 缓冲
 *   - 丢失一帧无所谓（下一帧 16ms 后到）
 *   - 无连接管理开销
 */

#include "udpInput.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "gamepad/gamepadState.hpp"
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>
#include <task/task.hpp>

static constexpr char TAG[] = "UdpInput";
static constexpr uint16_t UDP_PORT = 8082;
static constexpr size_t FRAME_SIZE = 11;

// ════════════════════════════════════════════════════════════════
//  发送
// ════════════════════════════════════════════════════════════════

bool udpInputSend(const char* host, uint16_t port, const uint8_t* data, size_t len)
{
	if (!host || !data || len != FRAME_SIZE)
		return false;

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		ESP_LOGW(TAG, "socket create failed: errno=%d", errno);
		return false;
	}

	struct sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	inet_pton(AF_INET, host, &dest.sin_addr);

	ssize_t sent = sendto(sock, data, len, 0,
		(struct sockaddr*)&dest, sizeof(dest));

	close(sock);

	if (sent != (ssize_t)len)
	{
		ESP_LOGW(TAG, "sendto failed: sent=%d, errno=%d", sent, errno);
		return false;
	}
	return true;
}

// ════════════════════════════════════════════════════════════════
//  接收（后台 Task）
// ════════════════════════════════════════════════════════════════

/// 轻量来源追踪（记录哪些地址在发送，超时清理）
static struct {
	struct sockaddr_in addr;
	int8_t hostId;
	TickType_t lastSeen;
	bool active;
} s_sources[4] = {};

static void udpListenerTask(void* arg)
{
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		ESP_LOGE(TAG, "socket create failed: errno=%d", errno);
		vTaskDelete(nullptr);
		return;
	}

	struct sockaddr_in bindAddr{};
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(UDP_PORT);
	bindAddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0)
	{
		ESP_LOGE(TAG, "bind port %u failed: errno=%d", UDP_PORT, errno);
		close(sock);
		vTaskDelete(nullptr);
		return;
	}

	// 设置接收超时，定期检查 stale 源
	struct timeval tv{ .tv_sec = 5, .tv_usec = 0 };
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	ESP_LOGI(TAG, "listening on UDP port %u", UDP_PORT);

	while (true)
	{
		uint8_t buf[FRAME_SIZE];
		struct sockaddr_in from{};
		socklen_t fromLen = sizeof(from);

		ssize_t n = recvfrom(sock, buf, FRAME_SIZE, 0,
			(struct sockaddr*)&from, &fromLen);

		if (n < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// 超时正常，清理 stale sources
				TickType_t now = xTaskGetTickCount();
				for (int i = 1; i < 4; i++)
				{
					if (s_sources[i].active &&
						(now - s_sources[i].lastSeen) > pdMS_TO_TICKS(60000))
					{
						ESP_LOGI(TAG, "source H%d timed out", i);
						int8_t hostId = s_sources[i].hostId;
						s_sources[i].active = false;
						for (int p = 0; p < 4; p++)
							VirtualGamepad::instance().resetPlayer(hostId, p);
					}
				}
				continue;
			}
			ESP_LOGW(TAG, "recvfrom error: errno=%d", errno);
			continue;
		}

		if ((size_t)n != FRAME_SIZE)
			continue;

		// 解析帧
		int8_t hostId = (int8_t)buf[0];
		int8_t localPlayer = (int8_t)buf[1];

		GamepadState state{};
		state.buttons = (uint16_t)(buf[2] | (buf[3] << 8));
		state.lx = buf[4];
		state.ly = buf[5];
		state.rx = buf[6];
		state.ry = buf[7];
		state.lt = buf[8];
		state.rt = buf[9];
		state.dpad = buf[10];

		// 追踪源地址 → 更新最近活跃时间
		for (int i = 1; i < 4; i++)
		{
			if (s_sources[i].active && s_sources[i].hostId == hostId &&
				s_sources[i].addr.sin_addr.s_addr == from.sin_addr.s_addr)
			{
				s_sources[i].lastSeen = xTaskGetTickCount();
				break;
			}
		}

		ESP_LOGI(TAG, "Received UDP packet from %s:%d", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

		if (hostId >= 0 && hostId < 4)
			VirtualGamepad::instance().inject(hostId, localPlayer, state);
	}

	close(sock);
	vTaskDelete(nullptr);
}

void udpInputStart()
{
	// 使用 xTaskCreate 直接创建，udpListenerTask 含阻塞 recvfrom
	new Thread{ udpListenerTask, "udpInput", nullptr, Thread::Priority::RealTime };
	ESP_LOGI(TAG, "UDP input listener task added");
}
