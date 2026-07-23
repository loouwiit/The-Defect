/**
 * @file testHandler.cpp
 * @brief /ws/test WebSocket handler — 接收→打印→Echo
 */

#include "testHandler.hpp"
#include "wsServer/wsServer.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>

static constexpr char TAG[] = "WSTest";

static constexpr size_t MAX_MSG_LEN = 1024;

esp_err_t testWsHandler(httpd_req_t* req)
{
    // ── 握手阶段 ──
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WebSocket handshake /ws/test");
        return ESP_OK;
    }

    // ── 数据阶段：接收文本帧 ──
    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(wsPkt));
    wsPkt.type = HTTPD_WS_TYPE_TEXT;

    // 获取帧长度
    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "recv_frame (get len) failed: %d", ret);
        return ret;
    }

    if (wsPkt.len == 0) return ESP_OK;
    if (wsPkt.len > MAX_MSG_LEN)
    {
        ESP_LOGW(TAG, "message too long: %zu", wsPkt.len);
        uint8_t* discard = (uint8_t*)heap_caps_malloc(wsPkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (discard)
        {
            wsPkt.payload = discard;
            httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
            heap_caps_free(discard);
        }
        return ESP_OK;
    }

    // 分配并接收数据
    char* buf = (char*)heap_caps_malloc(wsPkt.len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf)
    {
        ESP_LOGE(TAG, "OOM");
        return ESP_ERR_NO_MEM;
    }

    wsPkt.payload = (uint8_t*)buf;
    ret = httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
    if (ret != ESP_OK)
    {
        heap_caps_free(buf);
        return ret;
    }
    buf[wsPkt.len] = '\0';

    ESP_LOGI(TAG, "收到: %s", buf);

    // ── Echo 回复 ──
    char reply[MAX_MSG_LEN + 32];
    int n = snprintf(reply, sizeof(reply), "echo: %s", buf);
    heap_caps_free(buf);

    if (n > 0)
    {
        int fd = httpd_req_to_sockfd(req);
        wsServerSendText(fd, reply, (size_t)n);
    }

    return ESP_OK;
}
