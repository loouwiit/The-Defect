/**
 * @file wsTouchHandler.cpp
 * @brief /ws/touch — 接收 Web 触摸坐标，注入 VirtualIndev
 */

#include "wsTouchHandler.hpp"
#include "virtualIndev.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>

static constexpr char TAG[] = "WSTouch";

esp_err_t wsTouchHandler(httpd_req_t* req)
{
    /* --- 握手阶段 --- */
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WebSocket handshake /ws/touch");
        return ESP_OK;
    }

    /* --- 数据阶段: 接收 WebSocket 帧 --- */
    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(wsPkt));
    wsPkt.type = HTTPD_WS_TYPE_BINARY;

    /* 第一步: 获取帧长度 */
    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "httpd_ws_recv_frame (get len) failed: %d", ret);
        return ret;
    }

    /* 第二步: 只接受 12 字节的二进制帧 */
    if (wsPkt.len != 12)
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

    /* 分配 12 字节缓冲区 (PSRAM) */
    uint8_t* buf = (uint8_t*)heap_caps_malloc(12, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf)
    {
        ESP_LOGE(TAG, "OOM");
        return ESP_ERR_NO_MEM;
    }

    wsPkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &wsPkt, 12);
    if (ret != ESP_OK)
    {
        heap_caps_free(buf);
        return ret;
    }

    /* 解析 3×int32 小端序 */
    int32_t x = (int32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
    int32_t y = (int32_t)(buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24));
    int32_t t = (int32_t)(buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24));

    heap_caps_free(buf);

    /* 注入到 LVGL */
    {
        lv_point_t pt{ (lv_coord_t)x, (lv_coord_t)y };
        lv_indev_state_t state = (t == 2) ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;
        VirtualIndev::instance().sendTouch(state, pt);
    }

    return ESP_OK;
}
