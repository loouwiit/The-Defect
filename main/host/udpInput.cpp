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

int udpInputCreateSocket(const char* host, uint16_t port)
{
	if (!host)
		return -1;

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		ESP_LOGW(TAG, "socket create failed: errno=%d", errno);
		return -1;
	}

	// 预连接目标地址：后续 sendto 可用更快路径
	struct sockaddr_in dest{};
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	inet_pton(AF_INET, host, &dest.sin_addr);

	// connect 后可用 send() 代替 sendto()，略快
	if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0)
	{
		ESP_LOGW(TAG, "connect failed: errno=%d", errno);
		close(sock);
		return -1;
	}

	return sock;
}

bool udpInputSend(int sock, const uint8_t* data, size_t len)
{
	if (sock < 0 || !data || len != FRAME_SIZE)
		return false;

	ssize_t sent = send(sock, data, len, 0);
	if (sent != (ssize_t)len)
	{
		ESP_LOGW(TAG, "send failed: sent=%d, errno=%d", sent, errno);
		return false;
	}
	return true;
}

void udpInputCloseSocket(int sock)
{
	if (sock >= 0)
		close(sock);
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

		// ⚠ 拒绝伪造 hostId=0：0 保留给本机 BLE 输入源，任何局域网设备都不得
		//   通过 UDP 注入本机玩家（防伪造输入）。
		if (hostId < 1 || hostId >= 4)
		{
			ESP_LOGW(TAG, "forged/invalid hostId=%d rejected", hostId);
			continue;
		}

		GamepadState state{};
		state.buttons = (uint16_t)(buf[2] | (buf[3] << 8));
		state.lx = buf[4];
		state.ly = buf[5];
		state.rx = buf[6];
		state.ry = buf[7];
		state.lt = buf[8];
		state.rt = buf[9];
		state.dpad = buf[10];

		// 追踪源地址 → 更新最近活跃时间 / 建立新源
		// ⚠ 原来 active 从不为 true → 60s 超时清理永不触发 → 远端断流后
		//   最后一帧（如按住 A 键）永久残留在 VirtualGamepad → 按键卡死。
		bool found = false;
		for (int i = 1; i < 4; i++)
		{
			if (s_sources[i].active && s_sources[i].hostId == hostId &&
				s_sources[i].addr.sin_addr.s_addr == from.sin_addr.s_addr)
			{
				s_sources[i].lastSeen = xTaskGetTickCount();
				found = true;
				break;
			}
		}
		if (!found)
		{
			// 按 hostId 映射槽位（1-3）；同 hostId 换 IP（主机 IP 变更）时更新
			int slot = hostId;
			if (!s_sources[slot].active ||
				s_sources[slot].addr.sin_addr.s_addr != from.sin_addr.s_addr)
			{
				s_sources[slot] = {};
				s_sources[slot].addr = from;
				s_sources[slot].hostId = hostId;
				s_sources[slot].lastSeen = xTaskGetTickCount();
				s_sources[slot].active = true;
				ESP_LOGI(TAG, "source H%d from %s", hostId, inet_ntoa(from.sin_addr));
			}
		}

		ESP_LOGD(TAG, "Received UDP packet from %s:%d", inet_ntoa(from.sin_addr), ntohs(from.sin_port));

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
