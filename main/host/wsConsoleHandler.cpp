#include "wsConsoleHandler.hpp"
#include "hostManager.hpp"
#include "virtualGamepad.hpp"
#include "wsServer/wsServer.hpp"
#include "bleGamepad/gamepadState.hpp"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cJSON.h>

static constexpr char TAG[] = "WsHost";

// ============================================================
//  /ws/console — 管理通道
// ============================================================

static esp_err_t consoleHandler(httpd_req_t* req)
{
    // 握手阶段
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "console handshake");
        return ESP_OK;
    }

    // 数据阶段: 接收文本帧
    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(wsPkt));
    wsPkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
    if (ret != ESP_OK) return ret;

    if (wsPkt.len == 0) return ESP_OK;
    if (wsPkt.len > 2048) {
        // 丢弃过大帧
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
    auto& hostMgr = HostManager::instance();

    // 解析 JSON
    cJSON* root = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!root) return ESP_OK;

    cJSON* typeItem = cJSON_GetObjectItem(root, "type");
    if (!typeItem || !cJSON_IsString(typeItem)) { cJSON_Delete(root); return ESP_OK; }
    const char* type = typeItem->valuestring;

    if (strcmp(type, "register") == 0) {
        // 从机注册
        cJSON* hostnameItem = cJSON_GetObjectItem(root, "hostname");
        cJSON* gpCountItem = cJSON_GetObjectItem(root, "gamepad_count");
        const char* hostname = (hostnameItem && cJSON_IsString(hostnameItem))
                               ? hostnameItem->valuestring : "unknown";
        int gpCount = (gpCountItem && cJSON_IsNumber(gpCountItem))
                      ? gpCountItem->valueint : 0;

        int slot = hostMgr.registerClient(fd, hostname, gpCount);

        // 构造全量槽位数组
        auto buildSlotsArray = []() -> cJSON* {
            cJSON* arr = cJSON_CreateArray();
            const auto* slots = HostManager::instance().getSlots();
            for (int i = 0; i < 4; i++) {
                cJSON* s = cJSON_CreateObject();
                cJSON_AddBoolToObject(s, "connected", slots[i].connected);
                if (slots[i].connected && slots[i].hostname[0])
                    cJSON_AddStringToObject(s, "hostname", slots[i].hostname);
                cJSON_AddNumberToObject(s, "gamepad_count", slots[i].gamepadCount);
                cJSON_AddItemToArray(arr, s);
            }
            return arr;
        };

        // 回复注册结果 + 全量槽位
        cJSON* resp = cJSON_CreateObject();
        if (slot >= 0) {
            cJSON_AddStringToObject(resp, "type", "registered");
            cJSON_AddNumberToObject(resp, "slot", slot);
            cJSON_AddItemToObject(resp, "slots", buildSlotsArray());
            ESP_LOGI(TAG, "client %s registered -> slot %d", hostname, slot);
        } else {
            cJSON_AddStringToObject(resp, "type", "register_failed");
            cJSON_AddStringToObject(resp, "reason", "full");
            ESP_LOGW(TAG, "client %s register failed (full)", hostname);
        }
        char* respStr = cJSON_PrintUnformatted(resp);
        if (respStr) {
            wsServerSendText(fd, respStr, strlen(respStr));
            cJSON_free(respStr);
        }
        cJSON_Delete(resp);

        // 广播全量槽位给所有客户端（包括新加入的）
        {
            cJSON* sync = cJSON_CreateObject();
            cJSON_AddStringToObject(sync, "type", "slot_sync");
            cJSON_AddItemToObject(sync, "slots", buildSlotsArray());
            char* syncStr = cJSON_PrintUnformatted(sync);
            if (syncStr) {
                wsServerBroadcastText(syncStr, strlen(syncStr));
                cJSON_free(syncStr);
            }
            cJSON_Delete(sync);
        }
    }
    else if (strcmp(type, "ping") == 0) {
        // 心跳回复
        cJSON* pong = cJSON_CreateObject();
        cJSON_AddStringToObject(pong, "type", "pong");
        char* pongStr = cJSON_PrintUnformatted(pong);
        if (pongStr) {
            wsServerSendText(fd, pongStr, strlen(pongStr));
            cJSON_free(pongStr);
        }
        cJSON_Delete(pong);
    }
    else if (strcmp(type, "input") == 0) {
        // 从机通过 console 通道发送的输入，解析后注入 VirtualGamepad
        cJSON* hi = cJSON_GetObjectItem(root, "hostId");
        cJSON* lp = cJSON_GetObjectItem(root, "localPlayer");
        cJSON* btns = cJSON_GetObjectItem(root, "buttons");
        cJSON* lx = cJSON_GetObjectItem(root, "lx");
        cJSON* ly = cJSON_GetObjectItem(root, "ly");
        cJSON* rx = cJSON_GetObjectItem(root, "rx");
        cJSON* ry = cJSON_GetObjectItem(root, "ry");
        cJSON* lt = cJSON_GetObjectItem(root, "lt");
        cJSON* rt = cJSON_GetObjectItem(root, "rt");
        cJSON* dpad = cJSON_GetObjectItem(root, "dpad");

        int inputHostId = (hi && cJSON_IsNumber(hi)) ? hi->valueint : -1;
        int localPlayer = (lp && cJSON_IsNumber(lp)) ? lp->valueint : 0;

        if (inputHostId >= 0) {
            GamepadState state;
            if (btns) state.buttons = (uint16_t)btns->valueint;
            if (lx) state.lx = (uint8_t)lx->valueint;
            if (ly) state.ly = (uint8_t)ly->valueint;
            if (rx) state.rx = (uint8_t)rx->valueint;
            if (ry) state.ry = (uint8_t)ry->valueint;
            if (lt) state.lt = (uint8_t)lt->valueint;
            if (rt) state.rt = (uint8_t)rt->valueint;
            if (dpad) state.dpad = (uint8_t)dpad->valueint;

            VirtualGamepad::instance().inject((int8_t)inputHostId, (int8_t)localPlayer, state);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================
//  /ws/input — 输入通道（从机→主机的手柄状态转发）
// ============================================================

static esp_err_t inputHandler(httpd_req_t* req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "input handshake");
        return ESP_OK;
    }

    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(wsPkt));
    wsPkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
    if (ret != ESP_OK) return ret;

    if (wsPkt.len == 0) return ESP_OK;
    if (wsPkt.len > 1024) {
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

    // 解析 JSON — 提取 GamepadState
    cJSON* root = cJSON_Parse(buf);
    heap_caps_free(buf);
    if (!root) return ESP_OK;

    cJSON* typeItem = cJSON_GetObjectItem(root, "type");
    if (!typeItem || !cJSON_IsString(typeItem)) { cJSON_Delete(root); return ESP_OK; }

    if (strcmp(typeItem->valuestring, "input") == 0) {
        // 从消息中直接取 hostId（由从机在发送时填入 myAssignedSlot）
        // 不依赖 fd 查找，因为 /ws/input 连接未注册到 fdMap
        cJSON* hi = cJSON_GetObjectItem(root, "hostId");
        cJSON* lp = cJSON_GetObjectItem(root, "localPlayer");
        cJSON* btns = cJSON_GetObjectItem(root, "buttons");
        cJSON* lx = cJSON_GetObjectItem(root, "lx");
        cJSON* ly = cJSON_GetObjectItem(root, "ly");
        cJSON* rx = cJSON_GetObjectItem(root, "rx");
        cJSON* ry = cJSON_GetObjectItem(root, "ry");
        cJSON* lt = cJSON_GetObjectItem(root, "lt");
        cJSON* rt = cJSON_GetObjectItem(root, "rt");
        cJSON* dpad = cJSON_GetObjectItem(root, "dpad");

        int inputHostId = (hi && cJSON_IsNumber(hi)) ? hi->valueint : -1;
        int localPlayer = (lp && cJSON_IsNumber(lp)) ? lp->valueint : 0;

        if (inputHostId < 0) {
            ESP_LOGW(TAG, "input missing hostId, dropping");
            cJSON_Delete(root);
            return ESP_OK;
        }

        ESP_LOGI(TAG, "input hostId=%d lp=%d btn=0x%04x",
                 inputHostId, localPlayer,
                 (btns && cJSON_IsNumber(btns)) ? (uint16_t)btns->valueint : 0);

        GamepadState state;
        if (btns) state.buttons = (uint16_t)btns->valueint;
        if (lx) state.lx = (uint8_t)lx->valueint;
        if (ly) state.ly = (uint8_t)ly->valueint;
        if (rx) state.rx = (uint8_t)rx->valueint;
        if (ry) state.ry = (uint8_t)ry->valueint;
        if (lt) state.lt = (uint8_t)lt->valueint;
        if (rt) state.rt = (uint8_t)rt->valueint;
        if (dpad) state.dpad = (uint8_t)dpad->valueint;

        VirtualGamepad::instance().inject((int8_t)inputHostId, (int8_t)localPlayer, state);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================
//  注册
// ============================================================

bool registerHostWsHandlers()
{
    if (!wsServerRegisterWs("/ws/console", consoleHandler)) {
        ESP_LOGE(TAG, "register /ws/console failed");
        return false;
    }
    if (!wsServerRegisterWs("/ws/input", inputHandler)) {
        ESP_LOGE(TAG, "register /ws/input failed");
        return false;
    }

    // 设置 HostManager 的广播函数（用于 slot_swap 等事件通知所有从机）
    HostManager::instance().setBroadcastFn([](const char* data, size_t len) {
        wsServerBroadcastText(data, len);
    });

    ESP_LOGI(TAG, "host WS handlers registered");
    return true;
}
