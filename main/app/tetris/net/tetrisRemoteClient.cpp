#include "tetrisRemoteClient.hpp"
#include "tetris_net.hpp"
#include <esp_log.h>
#include <cJSON.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>

static constexpr char TAG[] = "TetrisRemoteClient";

// ============================================================
//  构造 / 析构
// ============================================================

TetrisRemoteClient::TetrisRemoteClient() = default;

TetrisRemoteClient::~TetrisRemoteClient()
{
	disconnect();
}

// ============================================================
//  TCP 连接 / 断开
// ============================================================

bool TetrisRemoteClient::connect(const char* host, uint16_t port, int targetPlayer)
{
	m_targetPlayer = targetPlayer;

	// 创建 socket
	m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_sock < 0) {
		ESP_LOGE(TAG, "socket create failed: %d", errno);
		return false;
	}

	// 连接
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	struct hostent* he = gethostbyname(host);
	if (he && he->h_addr_list[0]) {
		memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
	} else {
		inet_pton(AF_INET, host, &addr.sin_addr);
	}

	ESP_LOGI(TAG, "connecting to %s:%d (target=%d)...", host, port, targetPlayer);

	if (::connect(m_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		ESP_LOGE(TAG, "connect failed: %d", errno);
		close(m_sock);
		m_sock = -1;
		return false;
	}

	// TCP_NODELAY
	int flag = 1;
	setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	// 启动接收 Task
	m_running = true;
	xTaskCreate(recvTaskFn, "tetrisTcpRecv", 4096, this, tskIDLE_PRIORITY + 3, &m_recvTask);

	ESP_LOGI(TAG, "TCP connected to %s:%d", host, port);
	return true;
}

void TetrisRemoteClient::disconnect()
{
	m_running = false;

	if (m_sock >= 0) {
		shutdown(m_sock, SHUT_RDWR);
		close(m_sock);
		m_sock = -1;
	}

	// 等接收 Task 退出
	if (m_recvTask) {
		vTaskDelay(pdMS_TO_TICKS(50));
		m_recvTask = nullptr;
	}

	Lock lock(m_mutex);
	m_hasNewState = false;
}

// ============================================================
//  发送
// ============================================================

void TetrisRemoteClient::sendInput(const char* key, bool down)
{
	int sock = m_sock;
	if (sock < 0) return;

	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", TetrisMsgTag::INPUT);
	cJSON_AddStringToObject(root, "key", key);
	cJSON_AddBoolToObject(root, "down", down);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);

	if (str) {
		char sendBuf[512];
		int slen = snprintf(sendBuf, sizeof(sendBuf), "%s\n", str);
		send(sock, sendBuf, slen, 0);
		cJSON_free(str);
	}
}

void TetrisRemoteClient::sendJoin()
{
	int sock = m_sock;
	if (sock < 0) {
		ESP_LOGW(TAG, "not connected, cannot send join");
		return;
	}

	ESP_LOGI(TAG, "sending join (target=%d)...", m_targetPlayer);

	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", TetrisMsgTag::JOIN);
	cJSON_AddNumberToObject(root, "target", m_targetPlayer);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);

	if (str) {
		char sendBuf[256];
		int slen = snprintf(sendBuf, sizeof(sendBuf), "%s\n", str);
		int ret = send(sock, sendBuf, slen, 0);
		if (ret > 0) {
			ESP_LOGI(TAG, "join sent (%d bytes)", ret);
		} else {
			ESP_LOGE(TAG, "join send failed: %d, errno=%d", ret, errno);
		}
		cJSON_free(str);
	}
}

// ============================================================
//  获取状态
// ============================================================

GameState TetrisRemoteClient::getGameState() const
{
	Lock lock(m_mutex);
	return m_state;
}

// ============================================================
//  接收 Task — 后台持续读 TCP，按 \n 分割 JSON
// ============================================================

void TetrisRemoteClient::recvTaskFn(void* arg)
{
	auto* self = static_cast<TetrisRemoteClient*>(arg);
	char buf[256];

	while (self->m_running && self->m_sock >= 0) {
		int n = recv(self->m_sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) {
			if (n == 0) {
				ESP_LOGI(TAG, "connection closed by peer");
			} else {
				ESP_LOGW(TAG, "recv error: %d, errno=%d", n, errno);
			}
			break;
		}
		self->processRecvData(buf, (size_t)n);
	}

	self->m_running = false;
	if (self->m_sock >= 0) {
		close(self->m_sock);
		self->m_sock = -1;
	}

	ESP_LOGW(TAG, "disconnected");
	if (self->m_onDisconnected) {
		self->m_onDisconnected();
	}

	self->m_recvTask = nullptr;
	vTaskDelete(nullptr);
}

void TetrisRemoteClient::processRecvData(const char* data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		char ch = data[i];
		if (ch == '\n') {
			m_lineBuf[m_lineLen] = '\0';
			if (m_lineLen > 0) {
				processLine(m_lineBuf);
			}
			m_lineLen = 0;
		} else if (m_lineLen < LINE_BUF_SIZE - 1) {
			m_lineBuf[m_lineLen++] = ch;
		}
	}
}

void TetrisRemoteClient::processLine(const char* line)
{
	ESP_LOGV(TAG, "recv: %s", line);
	cJSON* root = cJSON_Parse(line);
	if (!root) {
		ESP_LOGW(TAG, "JSON parse failed: %s", line);
		return;
	}

	cJSON* typeItem = cJSON_GetObjectItem(root, "type");
	if (!typeItem || !cJSON_IsString(typeItem)) {
		cJSON_Delete(root);
		return;
	}
	const char* tag = typeItem->valuestring;

	if (strcmp(tag, TetrisMsgTag::JOIN_ACK) == 0) {
		cJSON* p = cJSON_GetObjectItem(root, "player_id");
		if (p && cJSON_IsNumber(p)) {
			m_playerId = p->valueint;
			ESP_LOGI(TAG, "joined as player %d", m_playerId);
		}
	}
	else if (strcmp(tag, TetrisMsgTag::SNAPSHOT) == 0) {
		GameState state;
		if (gameStateFromJson(root, state)) {
			Lock lock(m_mutex);
			m_state = state;
			m_hasNewState = true;
			ESP_LOGD(TAG, "snapshot received: score=%d lines=%d go=%d",
				state.score, state.totalLines, state.gameOver);
		}
	}

	cJSON_Delete(root);
}
