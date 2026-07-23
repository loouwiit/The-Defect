#pragma once

#include <esp_http_server.h>

/**
 * @brief /ws/touch WebSocket handler
 *
 * 接收 12 字节二进制触摸帧（3×int32 小端序）：
 *   [0..3]  x 坐标
 *   [4..7]  y 坐标
 *   [8..11] type: 0=down, 1=move, 2=up
 *
 * 注入到 VirtualIndev → LVGL。
 * 在 wsServerStart() 后调用 wsServerRegisterWs() 注册。
 */
esp_err_t wsTouchHandler(httpd_req_t* req);
