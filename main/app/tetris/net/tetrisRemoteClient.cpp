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

bool TetrisRemoteClient::listen(int port)
{
	// 创建 listen socket
	int listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock < 0) {
		ESP_LOGE(TAG, "socket create failed: %d", errno);
		return false;
	}

	int opt = 1;
	setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		ESP_LOGE(TAG, "bind port %d failed: %d", port, errno);
		close(listenSock);
		return false;
	}

	if (::listen(listenSock, 1) < 0) {
		ESP_LOGE(TAG, "listen failed: %d", errno);
		close(listenSock);
		return false;
	}

	ESP_LOGI(TAG, "listening on port %d, waiting for host...", port);

	// 阻塞等待主机连接
	struct sockaddr_in peer;
	socklen_t peerLen = sizeof(peer);
	m_sock = accept(listenSock, (struct sockaddr*)&peer, &peerLen);
	close(listenSock);  // 不再接受其他连接

	if (m_sock < 0) {
		ESP_LOGE(TAG, "accept failed: %d", errno);
		return false;
	}

	// TCP_NODELAY
	int flag = 1;
	setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	char peerStr[16];
	inet_ntop(AF_INET, &peer.sin_addr, peerStr, sizeof(peerStr));
	ESP_LOGI(TAG, "accepted connection from host %s", peerStr);

	// 启动接收 Task
	m_running = true;
	xTaskCreate(recvTaskFn, "tetrisTcpRecv", 4096, this, tskIDLE_PRIORITY + 3, &m_recvTask);

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

	for (auto& ns : m_hasNewState) ns = false;
}

// ============================================================
//  状态访问
// ============================================================

GameState TetrisRemoteClient::getPlayerState(int player) const
{
	Lock lock(m_mutex);
	if (player >= 0 && player < MAX_REMOTE_PLAYERS)
		return m_states[player];
	return GameState{};
}

bool TetrisRemoteClient::hasNewState(int player) const
{
	if (player >= 0 && player < MAX_REMOTE_PLAYERS)
		return m_hasNewState[player];
	return false;
}

void TetrisRemoteClient::consumeNewState(int player)
{
	if (player >= 0 && player < MAX_REMOTE_PLAYERS)
		m_hasNewState[player] = false;
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
		cJSON* pc = cJSON_GetObjectItem(root, "player_count");
		if (pc && cJSON_IsNumber(pc) && pc->valueint > 0) {
			m_playerCount = pc->valueint;
			ESP_LOGI(TAG, "game has %d players", m_playerCount);
		}
	}
	else if (strcmp(tag, TetrisMsgTag::SNAPSHOT) == 0) {
		cJSON* pItem = cJSON_GetObjectItem(root, "p");
		int player = pItem && cJSON_IsNumber(pItem) ? pItem->valueint : 0;
		if (player < 0 || player >= MAX_REMOTE_PLAYERS) player = 0;

		GameState state;
		if (gameStateFromJson(root, state)) {
			Lock lock(m_mutex);
			m_states[player] = state;
			m_hasNewState[player] = true;
			if (player + 1 > m_playerCount) m_playerCount = player + 1;
			ESP_LOGD(TAG, "snapshot P%d: score=%d lines=%d go=%d",
				player, state.score, state.totalLines, state.gameOver);
		}
	}

	cJSON_Delete(root);
}
