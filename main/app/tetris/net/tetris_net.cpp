#include "tetris_net.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstring>
#include <cJSON.h>

static constexpr char TAG[] = "TetrisNet";

// ============================================================
//  内部状态
// ============================================================

namespace {

/// Host 模式下管理的客户端连接
struct ClientInfo {
    int  fd       = -1;
    int  playerId = -1;
    int  target   = 0;   // 客户端操作的目标玩家索引
    bool active   = false;
};

constexpr int MAX_CLIENTS   = 2;
constexpr int MAX_URI_LEN   = 128;
constexpr int MSG_QUEUE_LEN = 16;   // WS 任务→游戏循环的队列深度
constexpr int MSG_MAX_LEN   = 256;  // 单条消息最大长度

/// 全局状态
static struct {
    bool initialized = false;
    bool isHost      = false;

    // Host
    ClientInfo clients[MAX_CLIENTS];
    int        nextPlayerId = 1;

    // Client（通过 esp_websocket_client 实现）
    esp_websocket_client_handle_t wsClient = nullptr;
    QueueHandle_t                 msgQueue = nullptr;  // 接收消息队列
    int  myPlayerId = -1;

    // 回调
    TetrisHostCallbacks   hostCb;
    TetrisClientCallbacks clientCb;

} s;

} // anonymous namespace

// ============================================================
//  Client WS 事件 — 在 esp_websocket_client 任务上下文中运行
// ============================================================

static void clientEventHandler(void* handlerArgs, esp_event_base_t base,
                               int32_t eventId, void* eventData)
{
    auto* data = static_cast<esp_websocket_event_data_t*>(eventData);

    switch (eventId) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS client connected");
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WS client disconnected");
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 1 && data->data_len > 0) {
            size_t copyLen = (data->data_len < MSG_MAX_LEN) ? data->data_len : MSG_MAX_LEN - 1;
            char* buf = (char*)heap_caps_malloc(copyLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) break;
            memcpy(buf, data->data_ptr, copyLen);
            buf[copyLen] = '\0';
            if (s.msgQueue) xQueueSend(s.msgQueue, &buf, 0);
            else heap_caps_free(buf);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "WS client error");
        break;

    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(TAG, "WS client closed");
        break;

    default: break;
    }
}

/// 游戏循环中处理 Client 接收队列（线程安全）
static void processClientQueue()
{
    if (!s.msgQueue) return;
    char* buf = nullptr;
    while (xQueueReceive(s.msgQueue, &buf, 0) == pdTRUE && buf) {
        cJSON* root = cJSON_Parse(buf);
        heap_caps_free(buf);
        if (!root) continue;

        cJSON* t = cJSON_GetObjectItem(root, "type");
        if (!t || !cJSON_IsString(t)) { cJSON_Delete(root); continue; }
        const char* tag = t->valuestring;

        if (strcmp(tag, TetrisMsgTag::JOIN_ACK) == 0) {
            cJSON* p = cJSON_GetObjectItem(root, "player_id");
            if (p && cJSON_IsNumber(p)) {
                s.myPlayerId = p->valueint;
                ESP_LOGI(TAG, "joined as player %d", s.myPlayerId);
                if (s.clientCb.onJoined) s.clientCb.onJoined(s.myPlayerId);
            }
        }
        else if (strcmp(tag, TetrisMsgTag::PIECE) == 0) {
            cJSON* y = cJSON_GetObjectItem(root, "piece");
            if (y && cJSON_IsString(y)) {
                PieceType type = pieceTypeFromChar(y->valuestring[0]);
                if (s.clientCb.onNewPiece) s.clientCb.onNewPiece(type);
            }
        }
        else if (strcmp(tag, TetrisMsgTag::GARBAGE) == 0) {
            cJSON* n = cJSON_GetObjectItem(root, "lines");
            if (n && cJSON_IsNumber(n)) {
                if (s.clientCb.onGarbage) s.clientCb.onGarbage(n->valueint);
            }
        }
        else if (strcmp(tag, TetrisMsgTag::WINNER) == 0) {
            cJSON* p = cJSON_GetObjectItem(root, "player_id");
            if (p && cJSON_IsNumber(p)) {
                if (s.clientCb.onWinner) s.clientCb.onWinner(p->valueint);
            }
        }
        else if (strcmp(tag, TetrisMsgTag::GAME_OVER) == 0) {
            cJSON* p = cJSON_GetObjectItem(root, "player_id");
            int pid = (p && cJSON_IsNumber(p)) ? p->valueint : -1;
            if (s.clientCb.onGameOver) s.clientCb.onGameOver(pid);
        }

        cJSON_Delete(root);
    }
}

// ============================================================
//  Host WebSocket handler — /ws/tetris
// ============================================================

static esp_err_t tetrisWsHandler(httpd_req_t* req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake /ws/tetris");
        return ESP_OK;
    }

    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(wsPkt));
    wsPkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
    if (ret != ESP_OK) return ret;
    if (wsPkt.len == 0) return ESP_OK;
    if (wsPkt.len > 4096) {
        uint8_t* d = (uint8_t*)heap_caps_malloc(wsPkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (d) { wsPkt.payload = d; httpd_ws_recv_frame(req, &wsPkt, wsPkt.len); heap_caps_free(d); }
        return ESP_OK;
    }

    char* buf = (char*)heap_caps_malloc(wsPkt.len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return ESP_ERR_NO_MEM;
    wsPkt.payload = (uint8_t*)buf;
    ret = httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
    if (ret != ESP_OK) { heap_caps_free(buf); return ret; }
    buf[wsPkt.len] = '\0';

    int fd = httpd_req_to_sockfd(req);

    cJSON* root = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!root) return ESP_OK;

    cJSON* t = cJSON_GetObjectItem(root, "type");
    if (!t || !cJSON_IsString(t)) { cJSON_Delete(root); return ESP_OK; }
    const char* tag = t->valuestring;

    if (s.isHost) {
        if (strcmp(tag, TetrisMsgTag::JOIN) == 0) {
            // 如果相同 fd 的旧客户端还在，先清理（断线重连）
            for (auto& c : s.clients) {
                if (c.active && c.fd == fd) { c.active = false; break; }
            }

            int pid = s.nextPlayerId++;
            int target = 0;
            cJSON* tgt = cJSON_GetObjectItem(root, "target");
            if (tgt && cJSON_IsNumber(tgt)) target = tgt->valueint;
            if (target < 0) target = 0;
            if (target >= MAX_CLIENTS) target = MAX_CLIENTS - 1;

            for (auto& c : s.clients) {
                if (!c.active) { c.fd = fd; c.playerId = pid; c.target = target; c.active = true; break; }
            }
            cJSON* ack = cJSON_CreateObject();
            cJSON_AddStringToObject(ack, "type", TetrisMsgTag::JOIN_ACK);
            cJSON_AddNumberToObject(ack, "player_id", pid);
            cJSON_AddNumberToObject(ack, "target", target);
            char* ackStr = cJSON_PrintUnformatted(ack);
            if (ackStr) { wsServerSendText(fd, ackStr, strlen(ackStr)); cJSON_free(ackStr); }
            cJSON_Delete(ack);
            ESP_LOGI(TAG, "player %d joined (fd=%d), target=%d", pid, fd, target);
            if (s.hostCb.onJoin) s.hostCb.onJoin(pid, fd);
        }
        else if (strcmp(tag, TetrisMsgTag::ATTACK) == 0) {
            cJSON* to   = cJSON_GetObjectItem(root, "target");
            cJSON* lines = cJSON_GetObjectItem(root, "lines");
            if (to && lines && cJSON_IsNumber(to) && cJSON_IsNumber(lines)) {
                if (s.hostCb.onAttack) s.hostCb.onAttack(to->valueint, lines->valueint);
            }
        }
        else if (strcmp(tag, TetrisMsgTag::INPUT) == 0) {
            cJSON* key  = cJSON_GetObjectItem(root, "key");
            cJSON* down = cJSON_GetObjectItem(root, "down");
            if (key && cJSON_IsString(key) && down && cJSON_IsBool(down)) {
                int target = 0;
                for (auto& c : s.clients) { if (c.fd == fd) { target = c.target; break; } }
                if (s.hostCb.onInput)
                    s.hostCb.onInput(target, key->valuestring, cJSON_IsTrue(down));
            }
        }
        else if (strcmp(tag, TetrisMsgTag::GAME_OVER) == 0) {
            int pid = -1;
            for (auto& c : s.clients) { if (c.fd == fd) { pid = c.playerId; break; } }
            if (pid >= 0 && s.hostCb.onGameOver) s.hostCb.onGameOver(pid);
        }
        else if (strcmp(tag, TetrisMsgTag::PING) == 0) {
            cJSON* pong = cJSON_CreateObject();
            cJSON_AddStringToObject(pong, "type", TetrisMsgTag::PONG);
            char* pongStr = cJSON_PrintUnformatted(pong);
            if (pongStr) { wsServerSendText(fd, pongStr, strlen(pongStr)); cJSON_free(pongStr); }
            cJSON_Delete(pong);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================
//  工具函数
// ============================================================

static char* buildSimpleJson(const char* tag)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", tag);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

static char* buildNumJson(const char* tag, const char* key, int val)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", tag);
    cJSON_AddNumberToObject(root, key, val);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

// ============================================================
//  API 实现
// ============================================================

bool tetrisNetInit(bool isHost,
                   const TetrisHostCallbacks& hostCb,
                   const TetrisClientCallbacks& clientCb)
{
    if (s.initialized) return true;

    s.isHost = isHost;
    s.hostCb = hostCb;
    s.clientCb = clientCb;

    if (!wsServerRegisterWs("/ws/tetris", tetrisWsHandler)) {
        ESP_LOGE(TAG, "failed to register /ws/tetris");
        return false;
    }

    // Client 模式创建消息队列
    if (!isHost) {
        s.msgQueue = xQueueCreate(MSG_QUEUE_LEN, sizeof(char*));
        if (!s.msgQueue) {
            ESP_LOGE(TAG, "msg queue create failed");
            wsServerUnregister("/ws/tetris");
            return false;
        }
    }

    s.initialized = true;
    ESP_LOGI(TAG, "initialized (%s mode)", isHost ? "Host" : "Client");
    return true;
}

void tetrisNetDeinit()
{
    if (!s.initialized) return;

    tetrisNetClientDisconnect();

    // 清空消息队列
    if (s.msgQueue) {
        char* buf = nullptr;
        while (xQueueReceive(s.msgQueue, &buf, 0) == pdTRUE) heap_caps_free(buf);
        vQueueDelete(s.msgQueue);
        s.msgQueue = nullptr;
    }

    for (auto& c : s.clients) c = {};
    wsServerUnregister("/ws/tetris");
    s.initialized = false;
    s.isHost = false;
    ESP_LOGI(TAG, "deinitialized");
}

void tetrisNetUpdate()
{
    if (s.initialized && !s.isHost) {
        processClientQueue();
    }
}

// ============================================================
//  Host API
// ============================================================

int tetrisNetHostGetFirstClientFd()
{
    if (!s.initialized || !s.isHost) return -1;
    for (auto& c : s.clients) {
        if (c.active) return c.fd;
    }
    return -1;
}

int tetrisNetHostGetClientTarget()
{
    if (!s.initialized || !s.isHost) return 0;
    for (auto& c : s.clients) {
        if (c.active) return c.target;
    }
    return 0;
}

void tetrisNetHostSendPiece(int fd, PieceType type)
{
    if (!s.initialized || !s.isHost) return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::PIECE);
    char ts[2] = { pieceTypeToChar(type), '\0' };
    cJSON_AddStringToObject(root, "piece", ts);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) { wsServerSendText(fd, str, strlen(str)); cJSON_free(str); }
}

void tetrisNetHostBroadcastPiece(PieceType type)
{
    if (!s.initialized || !s.isHost) return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::PIECE);
    char ts[2] = { pieceTypeToChar(type), '\0' };
    cJSON_AddStringToObject(root, "piece", ts);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) {
        for (auto& c : s.clients) { if (c.active) wsServerSendText(c.fd, str, strlen(str)); }
        cJSON_free(str);
    }
}

void tetrisNetHostSendGarbage(int fd, int lines)
{
    if (!s.initialized || !s.isHost) return;
    char* json = buildNumJson(TetrisMsgTag::GARBAGE, "lines", lines);
    if (json) { wsServerSendText(fd, json, strlen(json)); cJSON_free(json); }
}

void tetrisNetHostSendBoardSnapshot(int fd, int playerIndex,
    const Board& board, const Piece& curPiece, int ghostY,
    int score, int level, int lines,
    const PieceType* nextPieces, int nextCount,
    PieceType holdPiece, int pendingGarbage)
{
    if (!s.initialized || !s.isHost) return;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::BOARD_FULL);
    cJSON_AddNumberToObject(root, "player", playerIndex);

    // 棋盘: 20 可见行 × 10 列, 从底部(y=0)到顶部(y=19)
    cJSON* cells = cJSON_CreateArray();
    for (int y = 0; y < BOARD_VISIBLE_H; y++) {
        int row = BOARD_HEIGHT - 1 - y;  // y=0 → row=21 (底), y=19 → row=2 (顶)
        for (int x = 0; x < BOARD_WIDTH; x++) {
            cJSON_AddItemToArray(cells, cJSON_CreateNumber(board.data()[row * BOARD_WIDTH + x]));
        }
    }
    cJSON_AddItemToObject(root, "board", cells);

    // 当前方块
    cJSON* piece = cJSON_CreateObject();
    char ptype[2] = { pieceTypeToChar(curPiece.type()), '\0' };
    cJSON_AddStringToObject(piece, "type", ptype);
    if (curPiece.type() != PieceType::NONE) {
        cJSON_AddNumberToObject(piece, "x", curPiece.x());
        cJSON_AddNumberToObject(piece, "y", curPiece.y());
        cJSON_AddNumberToObject(piece, "r", static_cast<int>(curPiece.rotation()));
    }
    cJSON_AddItemToObject(root, "piece", piece);
    cJSON_AddNumberToObject(root, "ghost_y", ghostY);

    // 分数
    cJSON_AddNumberToObject(root, "score", score);
    cJSON_AddNumberToObject(root, "level", level);
    cJSON_AddNumberToObject(root, "lines", lines);

    // NEXT 预览
    cJSON* nextArr = cJSON_CreateArray();
    for (int i = 0; i < nextCount; i++) {
        char t[2] = { pieceTypeToChar(nextPieces[i]), '\0' };
        cJSON_AddItemToArray(nextArr, cJSON_CreateString(t));
    }
    cJSON_AddItemToObject(root, "next", nextArr);

    // Hold
    if (holdPiece != PieceType::NONE) {
        char t[2] = { pieceTypeToChar(holdPiece), '\0' };
        cJSON_AddStringToObject(root, "hold", t);
    }

    cJSON_AddNumberToObject(root, "garbage", pendingGarbage);

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) { wsServerSendText(fd, str, strlen(str)); cJSON_free(str); }
}

void tetrisNetHostBroadcastWinner(int playerId)
{
    if (!s.initialized || !s.isHost) return;
    char* json = buildNumJson(TetrisMsgTag::WINNER, "player_id", playerId);
    if (json) {
        for (auto& c : s.clients) { if (c.active) wsServerSendText(c.fd, json, strlen(json)); }
        cJSON_free(json);
    }
}

// ============================================================
//  Client API
// ============================================================

bool tetrisNetClientConnect(const char* host, uint16_t port)
{
    if (!s.initialized || s.isHost) return false;

    char uri[MAX_URI_LEN];
    int n = snprintf(uri, sizeof(uri), "ws://%s:%d/ws/tetris", host, port);
    if (n <= 0 || n >= (int)sizeof(uri)) { ESP_LOGE(TAG, "URI too long"); return false; }

    ESP_LOGI(TAG, "connecting to %s ...", uri);

    esp_websocket_client_config_t cfg = {};
    cfg.uri                    = uri;
    cfg.buffer_size            = 1024;
    cfg.task_stack             = 4096;
    cfg.reconnect_timeout_ms   = 5000;
    cfg.network_timeout_ms     = 10000;
    cfg.disable_auto_reconnect = true;

    s.wsClient = esp_websocket_client_init(&cfg);
    if (!s.wsClient) { ESP_LOGE(TAG, "ws client init failed"); return false; }

    esp_err_t ret = esp_websocket_register_events(
        s.wsClient, WEBSOCKET_EVENT_ANY, clientEventHandler, nullptr);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(s.wsClient);
        s.wsClient = nullptr;
        return false;
    }

    ret = esp_websocket_client_start(s.wsClient);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(s.wsClient);
        s.wsClient = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "WS client connecting...");
    return true;
}

void tetrisNetClientDisconnect()
{
    if (s.wsClient) {
        esp_websocket_client_stop(s.wsClient);
        esp_websocket_client_destroy(s.wsClient);
        s.wsClient = nullptr;
        ESP_LOGI(TAG, "WS client destroyed");
    }
    s.myPlayerId = -1;
}

bool tetrisNetClientIsConnected()
{
    return s.wsClient && esp_websocket_client_is_connected(s.wsClient);
}

void tetrisNetClientSendLock()
{
    if (!tetrisNetClientIsConnected()) return;
    char* json = buildSimpleJson(TetrisMsgTag::LOCK);
    if (json) {
        esp_websocket_client_send_text(s.wsClient, json, strlen(json), pdMS_TO_TICKS(1000));
        cJSON_free(json);
    }
}

void tetrisNetClientSendAttack(int toId, int lines)
{
    if (!tetrisNetClientIsConnected()) return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::ATTACK);
    cJSON_AddNumberToObject(root, "target", toId);
    cJSON_AddNumberToObject(root, "lines", lines);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) {
        esp_websocket_client_send_text(s.wsClient, str, strlen(str), pdMS_TO_TICKS(1000));
        cJSON_free(str);
    }
}

void tetrisNetClientSendGameOver()
{
    if (!tetrisNetClientIsConnected()) return;
    char* json = buildSimpleJson(TetrisMsgTag::GAME_OVER);
    if (json) {
        esp_websocket_client_send_text(s.wsClient, json, strlen(json), pdMS_TO_TICKS(1000));
        cJSON_free(json);
    }
}

void tetrisNetClientSendMove(PieceType type, int x, int y, int r)
{
    if (!tetrisNetClientIsConnected()) return;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::MOVE);
    char ts[2] = { pieceTypeToChar(type), '\0' };
    cJSON_AddStringToObject(root, "piece", ts);
    cJSON_AddNumberToObject(root, "x", x);
    cJSON_AddNumberToObject(root, "y", y);
    cJSON_AddNumberToObject(root, "r", r);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) {
        esp_websocket_client_send_text(s.wsClient, str, strlen(str), pdMS_TO_TICKS(1000));
        cJSON_free(str);
    }
}
