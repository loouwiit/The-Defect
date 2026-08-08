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

struct ClientInfo {
    int  fd       = -1;
    int  playerId = -1;
    int  target   = 0;
    bool active   = false;
};

constexpr int MAX_CLIENTS   = 2;
constexpr int MAX_URI_LEN   = 128;
constexpr int MSG_QUEUE_LEN = 16;
constexpr int MSG_MAX_LEN   = 4096;  // snapshot 可能较大

struct S {
    bool initialized = false;
    bool isHost      = false;

    // Host
    ClientInfo clients[MAX_CLIENTS];
    int        nextPlayerId = 1;

    // Client
    esp_websocket_client_handle_t wsClient = nullptr;
    QueueHandle_t                 msgQueue = nullptr;
    int  myPlayerId = -1;

    TetrisHostCallbacks   hostCb;
    TetrisClientCallbacks clientCb;
};
static S& getS()
{
    static S inst;
    return inst;
}

} // anonymous namespace

// ============================================================
//  GameState 序列化 / 反序列化
// ============================================================

static cJSON* gameStateToJson(const GameState& state, int playerIndex)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::SNAPSHOT);
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

    // current piece
    if (state.currentPieceType != PieceType::NONE) {
        char pt[2] = { pieceTypeToChar(state.currentPieceType), '\0' };
        cJSON_AddStringToObject(root, "pt", pt);
        cJSON_AddNumberToObject(root, "px", state.currentPieceX);
        cJSON_AddNumberToObject(root, "py", state.currentPieceY);
        cJSON_AddNumberToObject(root, "pr", static_cast<int>(state.currentPieceRotation));
    }

    // ghost
    cJSON_AddNumberToObject(root, "gx", state.ghostPieceX);
    cJSON_AddNumberToObject(root, "gy", state.ghostPieceY);

    // next preview
    cJSON* nextArr = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        char t[2] = { pieceTypeToChar(state.nextPieces[i]), '\0' };
        cJSON_AddItemToArray(nextArr, cJSON_CreateString(t));
    }
    cJSON_AddItemToObject(root, "n", nextArr);

    // hold
    if (state.holdPiece != PieceType::NONE) {
        char t[2] = { pieceTypeToChar(state.holdPiece), '\0' };
        cJSON_AddStringToObject(root, "h", t);
    }

    // score
    cJSON_AddNumberToObject(root, "sc", state.score);
    cJSON_AddNumberToObject(root, "lv", state.level);
    cJSON_AddNumberToObject(root, "ln", state.totalLines);
    cJSON_AddNumberToObject(root, "cb", state.combo);

    // battle
    cJSON_AddNumberToObject(root, "g", state.pendingGarbage);
    cJSON_AddNumberToObject(root, "gf", state.garbageFlash);
    cJSON_AddNumberToObject(root, "at", state.attackTarget);

    // status
    cJSON_AddBoolToObject(root, "go", state.gameOver);
    cJSON_AddBoolToObject(root, "ac", state.active);

    return root;
}

bool gameStateFromJson(const cJSON* root, GameState& out)
{
    // board
    cJSON* boardArr = cJSON_GetObjectItem(root, "b");
    if (boardArr && cJSON_IsArray(boardArr)) {
        int idx = 0;
        for (int y = 0; y < BOARD_VISIBLE_H && idx < cJSON_GetArraySize(boardArr); y++) {
            for (int x = 0; x < BOARD_WIDTH && idx < cJSON_GetArraySize(boardArr); x++, idx++) {
                cJSON* cell = cJSON_GetArrayItem(boardArr, idx);
                if (cell) {
                    // Board::data() is const, can't write through it
                    // We need a mutable approach. Since Board has no direct setter for raw index,
                    // use Board::set() or access m_cells. For now, use a workaround:
                    // Actually Board::set(col, y, val) exists. Let's use that.
                    out.board.set(x, y, static_cast<uint8_t>(cell->valueint));
                }
            }
        }
    }
    // JSON 反序列化后标记棋盘全部 dirty，供 Renderer 增量更新
    out.board.markAllDirty();

    // current piece
    cJSON* pt = cJSON_GetObjectItem(root, "pt");
    if (pt && cJSON_IsString(pt)) {
        out.currentPieceType = pieceTypeFromChar(pt->valuestring[0]);
    }
    cJSON* px = cJSON_GetObjectItem(root, "px");
    cJSON* py = cJSON_GetObjectItem(root, "py");
    cJSON* pr = cJSON_GetObjectItem(root, "pr");
    if (px) out.currentPieceX = px->valueint;
    if (py) out.currentPieceY = py->valueint;
    if (pr) out.currentPieceRotation = static_cast<Rotation>(pr->valueint);

    // ghost
    cJSON* gx = cJSON_GetObjectItem(root, "gx");
    if (gx) out.ghostPieceX = gx->valueint;
    cJSON* gy = cJSON_GetObjectItem(root, "gy");
    if (gy) out.ghostPieceY = gy->valueint;

    // next preview
    cJSON* nextArr = cJSON_GetObjectItem(root, "n");
    if (nextArr && cJSON_IsArray(nextArr)) {
        for (int i = 0; i < 4 && i < cJSON_GetArraySize(nextArr); i++) {
            cJSON* item = cJSON_GetArrayItem(nextArr, i);
            if (item && cJSON_IsString(item))
                out.nextPieces[i] = pieceTypeFromChar(item->valuestring[0]);
        }
    }

    // hold
    cJSON* h = cJSON_GetObjectItem(root, "h");
    if (h && cJSON_IsString(h))
        out.holdPiece = pieceTypeFromChar(h->valuestring[0]);

    // score
    cJSON* sc = cJSON_GetObjectItem(root, "sc"); if (sc) out.score = sc->valueint;
    cJSON* lv = cJSON_GetObjectItem(root, "lv"); if (lv) out.level = lv->valueint;
    cJSON* ln = cJSON_GetObjectItem(root, "ln"); if (ln) out.totalLines = ln->valueint;
    cJSON* cb = cJSON_GetObjectItem(root, "cb"); if (cb) out.combo = cb->valueint;

    // battle
    cJSON* g  = cJSON_GetObjectItem(root, "g");  if (g)  out.pendingGarbage = g->valueint;
    cJSON* gf = cJSON_GetObjectItem(root, "gf"); if (gf) out.garbageFlash = gf->valueint;
    cJSON* at = cJSON_GetObjectItem(root, "at"); if (at) out.attackTarget = at->valueint;

    // status
    cJSON* go = cJSON_GetObjectItem(root, "go"); if (go) out.gameOver = cJSON_IsTrue(go);
    cJSON* ac = cJSON_GetObjectItem(root, "ac"); if (ac) out.active = cJSON_IsTrue(ac);

    return true;
}

// ============================================================
//  Client WS 事件
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
            if (getS().msgQueue) xQueueSend(getS().msgQueue, &buf, 0);
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

static void processClientQueue()
{
    if (!getS().msgQueue) return;
    char* buf = nullptr;
    while (xQueueReceive(getS().msgQueue, &buf, 0) == pdTRUE && buf) {
        cJSON* root = cJSON_Parse(buf);
        heap_caps_free(buf);
        if (!root) continue;

        cJSON* t = cJSON_GetObjectItem(root, "type");
        if (!t || !cJSON_IsString(t)) { cJSON_Delete(root); continue; }
        const char* tag = t->valuestring;

        if (strcmp(tag, TetrisMsgTag::JOIN_ACK) == 0) {
            cJSON* p = cJSON_GetObjectItem(root, "player_id");
            if (p && cJSON_IsNumber(p)) {
                getS().myPlayerId = p->valueint;
                ESP_LOGI(TAG, "joined as player %d", getS().myPlayerId);
                if (getS().clientCb.onJoined) getS().clientCb.onJoined(getS().myPlayerId);
            }
        }
        else if (strcmp(tag, TetrisMsgTag::SNAPSHOT) == 0) {
            GameState state;
            gameStateFromJson(root, state);
            if (getS().clientCb.onSnapshot)
                getS().clientCb.onSnapshot(state);
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
    if (wsPkt.len > MSG_MAX_LEN) {
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

    cJSON* typeItem = cJSON_GetObjectItem(root, "type");
    if (!typeItem || !cJSON_IsString(typeItem)) { cJSON_Delete(root); return ESP_OK; }
    const char* tag = typeItem->valuestring;

    if (getS().isHost) {
        if (strcmp(tag, TetrisMsgTag::JOIN) == 0) {
            // 清理已断开的旧客户端（fd 已失效）
            for (auto& c : getS().clients) {
                if (!c.active) continue;
                auto info = httpd_ws_get_fd_info(req->handle, c.fd);
                if (info != HTTPD_WS_CLIENT_WEBSOCKET) {
                    ESP_LOGI(TAG, "cleanup stale client fd=%d", c.fd);
                    c = {};
                }
            }

            // 清理与新连接同 fd 的旧记录（极少数 fd 刚好被复用的情况）
            for (auto& c : getS().clients) {
                if (c.active && c.fd == fd) { c.active = false; break; }
            }

            int pid = getS().nextPlayerId++;
            int target = 0;
            cJSON* tgt = cJSON_GetObjectItem(root, "target");
            if (tgt && cJSON_IsNumber(tgt)) target = tgt->valueint;

            for (auto& c : getS().clients) {
                if (!c.active) {
                    c.fd = fd; c.playerId = pid; c.target = target; c.active = true;
                    break;
                }
            }

            cJSON* ack = cJSON_CreateObject();
            cJSON_AddStringToObject(ack, "type", TetrisMsgTag::JOIN_ACK);
            cJSON_AddNumberToObject(ack, "player_id", pid);
            cJSON_AddNumberToObject(ack, "target", target);
            char* ackStr = cJSON_PrintUnformatted(ack);
            if (ackStr) { wsServerSendText(fd, ackStr, strlen(ackStr)); cJSON_free(ackStr); }
            cJSON_Delete(ack);
            ESP_LOGI(TAG, "player %d joined (fd=%d), target=%d", pid, fd, target);
            if (getS().hostCb.onJoin) getS().hostCb.onJoin(pid, fd);
        }
        else if (strcmp(tag, TetrisMsgTag::INPUT) == 0) {
            cJSON* key  = cJSON_GetObjectItem(root, "key");
            cJSON* down = cJSON_GetObjectItem(root, "down");
            if (key && cJSON_IsString(key) && down && cJSON_IsBool(down)) {
                int target = 0;
                for (auto& c : getS().clients) { if (c.fd == fd) { target = c.target; break; } }
                if (getS().hostCb.onInput)
                    getS().hostCb.onInput(target, key->valuestring, cJSON_IsTrue(down));
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================
//  API 实现
// ============================================================

bool tetrisNetInit(bool isHost,
                   const TetrisHostCallbacks& hostCb,
                   const TetrisClientCallbacks& clientCb)
{
    if (getS().initialized) return true;

    getS().isHost = isHost;
    getS().hostCb = hostCb;
    getS().clientCb = clientCb;

    if (!wsServerRegisterWs("/ws/tetris", tetrisWsHandler)) {
        ESP_LOGE(TAG, "failed to register /ws/tetris");
        return false;
    }

    if (!isHost) {
        getS().msgQueue = xQueueCreate(MSG_QUEUE_LEN, sizeof(char*));
        if (!getS().msgQueue) {
            ESP_LOGE(TAG, "msg queue create failed");
            wsServerUnregister("/ws/tetris");
            return false;
        }
    }

    getS().initialized = true;
    ESP_LOGI(TAG, "initialized (%s mode)", isHost ? "Host" : "Client");
    return true;
}

void tetrisNetDeinit()
{
    if (!getS().initialized) return;

    tetrisNetClientDisconnect();

    if (getS().msgQueue) {
        char* buf = nullptr;
        while (xQueueReceive(getS().msgQueue, &buf, 0) == pdTRUE) heap_caps_free(buf);
        vQueueDelete(getS().msgQueue);
        getS().msgQueue = nullptr;
    }

    for (auto& c : getS().clients) c = {};
    wsServerUnregister("/ws/tetris");
    getS().initialized = false;
    getS().isHost = false;
    ESP_LOGI(TAG, "deinitialized");
}

void tetrisNetUpdate()
{
    if (getS().initialized && !getS().isHost) {
        processClientQueue();
    }
}

// ============================================================
//  Host API
// ============================================================

int tetrisNetHostGetFirstClientFd()
{
    if (!getS().initialized || !getS().isHost) return -1;
    for (auto& c : getS().clients) {
        if (c.active) return c.fd;
    }
    return -1;
}

int tetrisNetHostGetClientTarget()
{
    if (!getS().initialized || !getS().isHost) return 0;
    for (auto& c : getS().clients) {
        if (c.active) return c.target;
    }
    return 0;
}

void tetrisNetHostSendSnapshot(int fd, int playerIndex, const GameState& state)
{
    if (!getS().initialized || !getS().isHost) return;

    cJSON* root = gameStateToJson(state, playerIndex);
    if (!root) return;

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) {
        wsServerSendText(fd, str, strlen(str));
        cJSON_free(str);
    }
}

void tetrisNetHostBroadcastSnapshot(int playerIndex, const GameState& state)
{
    if (!getS().initialized || !getS().isHost) return;

    cJSON* root = gameStateToJson(state, playerIndex);
    if (!root) return;

    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) {
        for (auto& c : getS().clients) {
            if (c.active) wsServerSendText(c.fd, str, strlen(str));
        }
        cJSON_free(str);
    }
}

// ============================================================
//  Client API
// ============================================================

bool tetrisNetClientConnect(const char* host, uint16_t port)
{
    if (!getS().initialized || getS().isHost) return false;

    char uri[MAX_URI_LEN];
    int n = snprintf(uri, sizeof(uri), "ws://%s:%d/ws/tetris", host, port);
    if (n <= 0 || n >= (int)sizeof(uri)) { ESP_LOGE(TAG, "URI too long"); return false; }

    ESP_LOGI(TAG, "connecting to %s ...", uri);

    esp_websocket_client_config_t cfg = {};
    cfg.uri                    = uri;
    cfg.buffer_size            = 2048;
    cfg.task_stack             = 4096;
    cfg.reconnect_timeout_ms   = 5000;
    cfg.network_timeout_ms     = 10000;
    cfg.disable_auto_reconnect = true;

    getS().wsClient = esp_websocket_client_init(&cfg);
    if (!getS().wsClient) { ESP_LOGE(TAG, "ws client init failed"); return false; }

    esp_err_t ret = esp_websocket_register_events(
        getS().wsClient, WEBSOCKET_EVENT_ANY, clientEventHandler, nullptr);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(getS().wsClient);
        getS().wsClient = nullptr;
        return false;
    }

    ret = esp_websocket_client_start(getS().wsClient);
    if (ret != ESP_OK) {
        esp_websocket_client_destroy(getS().wsClient);
        getS().wsClient = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "WS client connecting...");
    return true;
}

void tetrisNetClientDisconnect()
{
    if (getS().wsClient) {
        esp_websocket_client_stop(getS().wsClient);
        esp_websocket_client_destroy(getS().wsClient);
        getS().wsClient = nullptr;
        ESP_LOGI(TAG, "WS client destroyed");
    }
    getS().myPlayerId = -1;
}

bool tetrisNetClientIsConnected()
{
    return getS().wsClient && esp_websocket_client_is_connected(getS().wsClient);
}

void tetrisNetClientSendInput(const char* key, bool down)
{
    if (!tetrisNetClientIsConnected()) return;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", TetrisMsgTag::INPUT);
    cJSON_AddStringToObject(root, "key", key);
    cJSON_AddBoolToObject(root, "down", down);
    char* str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (str) {
        esp_websocket_client_send_text(getS().wsClient, str, strlen(str), pdMS_TO_TICKS(1000));
        cJSON_free(str);
    }
}
