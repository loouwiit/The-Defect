/**
 * @file wsInputHandler.cpp
 * @brief /ws/input — 接收远程手柄输入，注入 VirtualGamepad
 *
 * 协议：11 字节二进制帧
 *   [0]   hostId       — 设备号 (0=本机, 1-3=远程)
 *   [1]   localPlayer  — 设备上的手柄编号 (0-3)
 *   [2-3] buttons      — uint16 小端序
 *   [4]   lx
 *   [5]   ly
 *   [6]   rx
 *   [7]   ry
 *   [8]   lt
 *   [9]   rt
 *   [10]  dpad
 */

#include "wsInputHandler.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "gamepad/gamepadState.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>

static constexpr char TAG[] = "WSInput";

static constexpr size_t FRAME_SIZE_OLD = 11;    // 单玩家帧（Web 客户端兼容）
static constexpr size_t FRAME_SIZE_BATCH = 42;  // 批量帧（P4 从机）

// 轻量 fd→hostId 追踪，用于断开时 resetPlayer
static struct {
	int fd;
	int8_t hostId;
} s_fdMap[4] = {};

static void trackFd(int fd, int8_t hostId)
{
	// 找到或覆盖
	int oldest = 0;
	for (int i = 0; i < 4; i++) {
		if (s_fdMap[i].fd == fd) { s_fdMap[i].hostId = hostId; return; }
		if (s_fdMap[i].fd < s_fdMap[oldest].fd) oldest = i;
	}
	s_fdMap[oldest] = { fd, hostId };
}

static void untrackFd(int fd)
{
	for (int i = 0; i < 4; i++) {
		if (s_fdMap[i].fd == fd) {
			int8_t hostId = s_fdMap[i].hostId;
			s_fdMap[i] = { -1, -1 };
			// 复位该 hostId 的所有 4 个玩家
			for (int p = 0; p < 4; p++)
				VirtualGamepad::instance().resetPlayer(hostId, p);
			ESP_LOGI(TAG, "cleaned up hostId=%d (fd=%d)", hostId, fd);
			return;
		}
	}
}

esp_err_t wsInputHandler(httpd_req_t* req)
{
	/* ── 握手阶段 ── */
	if (req->method == HTTP_GET)
	{
		ESP_LOGI(TAG, "WebSocket handshake /ws/input");
		return ESP_OK;
	}

	/* ── 数据阶段 ── */
	httpd_ws_frame_t wsPkt;
	memset(&wsPkt, 0, sizeof(wsPkt));
	wsPkt.type = HTTPD_WS_TYPE_BINARY;

	/* 获取帧长度 */
	esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
	if (ret != ESP_OK)
	{
		int fd = httpd_req_to_sockfd(req);
		untrackFd(fd);
		return ret;
	}

	/* 接受 11 字节（旧单玩家）或 42 字节（新批量）帧 */
	if (wsPkt.len != FRAME_SIZE_OLD && wsPkt.len != FRAME_SIZE_BATCH)
	{
		if (wsPkt.len > 0)
		{
			uint8_t* discard = (uint8_t*)heap_caps_malloc(wsPkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if (discard)
			{
				wsPkt.payload = discard;
				httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
				heap_caps_free(discard);
			}
		}
		return ESP_OK;
	}

	/* 分配缓冲区 */
	uint8_t* buf = (uint8_t*)heap_caps_malloc(wsPkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!buf)
	{
		ESP_LOGE(TAG, "OOM");
		return ESP_ERR_NO_MEM;
	}

	wsPkt.payload = buf;
	ret = httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
	if (ret != ESP_OK)
	{
		heap_caps_free(buf);
		return ret;
	}

	int fd = httpd_req_to_sockfd(req);

	if (wsPkt.len == FRAME_SIZE_BATCH) {
		/* 42 字节批量帧（P4 从机） */
		int8_t hostId = (int8_t)buf[0];
		uint8_t playerMask = buf[1];
		trackFd(fd, hostId);
		ESP_LOGI(TAG, "recv: hostId=%d, mask=0x%02X", hostId, playerMask);
		for (int p = 0; p < 4; p++) {
			if (!(playerMask & (1 << p))) continue;
			int off = 2 + p * 10;
			GamepadState state{};
			state.buttons = (uint16_t)(buf[off + 1] | (buf[off + 2] << 8));
			state.lx = buf[off + 3]; state.ly = buf[off + 4];
			state.rx = buf[off + 5]; state.ry = buf[off + 6];
			state.lt = buf[off + 7]; state.rt = buf[off + 8];
			state.dpad = buf[off + 9];
			VirtualGamepad::instance().inject(hostId, p, state);
		}
	} else {
		/* 11 字节帧（Web 客户端 gamepad.html） */
		int8_t hostId = (int8_t)buf[0];
		int8_t localPlayer = (int8_t)buf[1];
		trackFd(fd, hostId);
		GamepadState state{};
		state.buttons = (uint16_t)(buf[2] | (buf[3] << 8));
		state.lx = buf[4]; state.ly = buf[5];
		state.rx = buf[6]; state.ry = buf[7];
		state.lt = buf[8]; state.rt = buf[9];
		state.dpad = buf[10];
		ESP_LOGD(TAG, "web recv: hostId=%d, localPlayer=%d", hostId, localPlayer);
		VirtualGamepad::instance().inject(hostId, localPlayer, state);
	}

	heap_caps_free(buf);
	return ESP_OK;
}
