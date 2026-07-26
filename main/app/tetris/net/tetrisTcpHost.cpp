#include "tetrisTcpHost.hpp"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>
#include <cstdio>
#include <cJSON.h>

static constexpr char TAG[] = "TetrisTcpHost";
static constexpr int MAX_TCP_CLIENTS = 3;
static constexpr int LINE_BUF_SIZE = 2048;
constexpr int MSG_MAX_LEN   = 4096;

// ============================================================
//  内部状态
// ============================================================

namespace {

struct TcpClient {
	int  sock        = -1;
	int  target      = 0;
	bool active      = false;
	TaskHandle_t recvTask = nullptr;

	// 行缓冲区
	char lineBuf[LINE_BUF_SIZE]{};
	size_t lineLen = 0;
};

static struct {
	bool started = false;
	int  listenSock = -1;
	TcpClient clients[MAX_TCP_CLIENTS]{};
	TetrisTcpHostCallbacks cb;
	TaskHandle_t acceptTask = nullptr;
} s;

} // anonymous namespace

// ============================================================
//  TCP 连接读取 — 按 \n 分割 JSON
// ============================================================

static void tcpRecvTask(void* arg)
{
	auto* client = static_cast<TcpClient*>(arg);
	char buf[128];

	while (client->active) {
		int n = recv(client->sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) {
			if (n == 0) {
				ESP_LOGI(TAG, "tcp client disconnected");
			} else {
				ESP_LOGW(TAG, "tcp recv error %d", n);
			}
			break;
		}

		// 按 \n 分割处理
		for (int i = 0; i < n; i++) {
			char ch = buf[i];
			if (ch == '\n') {
				// 一行完整消息
				client->lineBuf[client->lineLen] = '\0';

				if (client->lineLen > 0) {
					// 解析 JSON
					cJSON* root = cJSON_Parse(client->lineBuf);
					if (root) {
						cJSON* typeItem = cJSON_GetObjectItem(root, "type");
						if (typeItem && cJSON_IsString(typeItem)) {
							const char* tag = typeItem->valuestring;

							if (strcmp(tag, "join") == 0 && s.cb.onJoin) {
								int target = 0;
								cJSON* tgt = cJSON_GetObjectItem(root, "target");
								if (tgt && cJSON_IsNumber(tgt)) target = tgt->valueint;
								client->target = target;

								ESP_LOGI(TAG, "tcp player joined, target=%d", target);

								// 发送 join_ack（用 target 作为 player_id）
								cJSON* ack = cJSON_CreateObject();
								cJSON_AddStringToObject(ack, "type", "join_ack");
								cJSON_AddNumberToObject(ack, "player_id", target);
								cJSON_AddNumberToObject(ack, "target", target);
								char* ackStr = cJSON_PrintUnformatted(ack);
								if (ackStr) {
									char sendBuf[1024];
									int slen = snprintf(sendBuf, sizeof(sendBuf), "%s\n", ackStr);
									send(client->sock, sendBuf, slen, 0);
									cJSON_free(ackStr);
								}
								cJSON_Delete(ack);

								s.cb.onJoin(target, 0);
							}
							else if (strcmp(tag, "input") == 0 && s.cb.onInput) {
								cJSON* key  = cJSON_GetObjectItem(root, "key");
								cJSON* down = cJSON_GetObjectItem(root, "down");
								if (key && cJSON_IsString(key) && down && cJSON_IsBool(down)) {
									ESP_LOGI(TAG, "input: target=%d key=%s down=%d",
										client->target, key->valuestring, cJSON_IsTrue(down));
									s.cb.onInput(client->target, key->valuestring, cJSON_IsTrue(down));
								}
							}
						}
						cJSON_Delete(root);
					}
				}

				client->lineLen = 0;
			} else if (client->lineLen < LINE_BUF_SIZE - 1) {
				client->lineBuf[client->lineLen++] = ch;
			}
		}
	}

	// 清理
	client->active = false;
	client->recvTask = nullptr;
	if (client->sock >= 0) {
		close(client->sock);
		client->sock = -1;
	}
	ESP_LOGI(TAG, "tcp recv task end");
	vTaskDelete(nullptr);
}

// ============================================================
//  接受连接任务
// ============================================================

static void acceptTask(void* arg)
{
	ESP_LOGI(TAG, "TCP server listening on port %d", (int)(uintptr_t)arg);
	int port = (int)(uintptr_t)arg;

	while (s.started && s.listenSock >= 0) {
		struct sockaddr_in addr;
		socklen_t addrLen = sizeof(addr);
		int clientSock = accept(s.listenSock, (struct sockaddr*)&addr, &addrLen);

		if (clientSock < 0) {
			if (s.started) ESP_LOGW(TAG, "accept failed: errno=%d", errno);
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		// 找个空闲槽位
		int slot = -1;
		for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
			if (!s.clients[i].active) { slot = i; break; }
		}

		if (slot < 0) {
			ESP_LOGW(TAG, "no free slot, rejecting connection");
			close(clientSock);
			continue;
		}

		// TCP_NODELAY
		int flag = 1;
		setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

		auto& c = s.clients[slot];
		c = {};
		c.sock = clientSock;
		c.active = true;

		// 创建接收 Task
		xTaskCreate(tcpRecvTask, "tcpGameRecv", 4096, &c,
			tskIDLE_PRIORITY + 3, &c.recvTask);

		char peerStr[16];
		inet_ntop(AF_INET, &addr.sin_addr, peerStr, sizeof(peerStr));
		ESP_LOGI(TAG, "tcp client connected: slot=%d, from=%s", slot, peerStr);
	}

	ESP_LOGI(TAG, "accept task end");
	vTaskDelete(nullptr);
}

// ============================================================
//  API 实现
// ============================================================

bool tetrisTcpHostStart(uint16_t port, const TetrisTcpHostCallbacks& cb)
{
	if (s.started) {
		ESP_LOGW(TAG, "already started");
		return true;
	}

	s.cb = cb;

	// 创建 listen socket
	s.listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s.listenSock < 0) {
		ESP_LOGE(TAG, "socket create failed: %d", errno);
		return false;
	}

	int opt = 1;
	setsockopt(s.listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(s.listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		ESP_LOGE(TAG, "bind port %d failed: %d", port, errno);
		close(s.listenSock);
		s.listenSock = -1;
		return false;
	}

	if (listen(s.listenSock, 3) < 0) {
		ESP_LOGE(TAG, "listen failed: %d", errno);
		close(s.listenSock);
		s.listenSock = -1;
		return false;
	}

	s.started = true;

	xTaskCreate(acceptTask, "tcpGameAccept", 4096,
		(void*)(uintptr_t)port, tskIDLE_PRIORITY + 2, &s.acceptTask);

	ESP_LOGI(TAG, "TCP game server started on port %d", port);
	return true;
}

void tetrisTcpHostStop()
{
	s.started = false;

	// 关闭所有客户端
	for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
		auto& c = s.clients[i];
		if (c.active) {
			c.active = false;
			if (c.sock >= 0) {
				shutdown(c.sock, SHUT_RDWR);
				close(c.sock);
				c.sock = -1;
			}
		}
	}

	// 关闭 listen
	if (s.listenSock >= 0) {
		close(s.listenSock);
		s.listenSock = -1;
	}

	// 等 task 退出
	vTaskDelay(pdMS_TO_TICKS(100));

	s.started = false;
	ESP_LOGI(TAG, "TCP game server stopped");
}

bool tetrisTcpHostHasClients()
{
	for (auto& c : s.clients) {
		if (c.active) return true;
	}
	return false;
}

void tetrisTcpHostBroadcastSnapshot(int playerIndex, const GameState& state)
{
	// 序列化 JSON
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", "snapshot");
	cJSON_AddNumberToObject(root, "p", playerIndex);

	// board: 20 可见行 × 10 列
	cJSON* boardArr = cJSON_CreateArray();
	for (int y = 0; y < BOARD_VISIBLE_H; y++) {
		int row = Board::yToRow(y);
		for (int x = 0; x < BOARD_WIDTH; x++) {
			cJSON_AddItemToArray(boardArr,
				cJSON_CreateNumber(state.board.data()[row * BOARD_WIDTH + x]));
		}
	}
	cJSON_AddItemToObject(root, "b", boardArr);

	if (state.currentPieceType != PieceType::NONE) {
		char pt[2] = { pieceTypeToChar(state.currentPieceType), '\0' };
		cJSON_AddStringToObject(root, "pt", pt);
		cJSON_AddNumberToObject(root, "px", state.currentPieceX);
		cJSON_AddNumberToObject(root, "py", state.currentPieceY);
		cJSON_AddNumberToObject(root, "pr", static_cast<int>(state.currentPieceRotation));
	}

	cJSON_AddNumberToObject(root, "gx", state.ghostPieceX);
	cJSON_AddNumberToObject(root, "gy", state.ghostPieceY);

	cJSON* nextArr = cJSON_CreateArray();
	for (int i = 0; i < 4; i++) {
		char t[2] = { pieceTypeToChar(state.nextPieces[i]), '\0' };
		cJSON_AddItemToArray(nextArr, cJSON_CreateString(t));
	}
	cJSON_AddItemToObject(root, "n", nextArr);

	if (state.holdPiece != PieceType::NONE) {
		char t[2] = { pieceTypeToChar(state.holdPiece), '\0' };
		cJSON_AddStringToObject(root, "h", t);
	}

	cJSON_AddNumberToObject(root, "sc", state.score);
	cJSON_AddNumberToObject(root, "lv", state.level);
	cJSON_AddNumberToObject(root, "ln", state.totalLines);
	cJSON_AddNumberToObject(root, "cb", state.combo);
	cJSON_AddNumberToObject(root, "g", state.pendingGarbage);
	cJSON_AddNumberToObject(root, "gf", state.garbageFlash);
	cJSON_AddNumberToObject(root, "at", state.attackTarget);
	cJSON_AddBoolToObject(root, "go", state.gameOver);
	cJSON_AddBoolToObject(root, "ac", state.active);

	char* jsonStr = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);

	if (!jsonStr) return;

	char sendBuf[MSG_MAX_LEN + 16];
	int slen = snprintf(sendBuf, sizeof(sendBuf), "%s\n", jsonStr);
	cJSON_free(jsonStr);

	// 发给所有活跃客户端
	for (auto& c : s.clients) {
		if (c.active) {
			int ret = send(c.sock, sendBuf, slen, 0);
			if (ret <= 0) {
				ESP_LOGW(TAG, "send to client failed: %d", ret);
			}
		}
	}

	ESP_LOGD(TAG, "snapshot broadcast: %d bytes to %d client(s)",
		slen, tetrisTcpHostHasClients());
}
