#pragma once

#include <esp_http_server.h>

/**
 * @brief /ws/console WebSocket handler
 *
 * 管理通道，处理设备注册/心跳/槽位广播/交换。
 * 协议（JSON 文本帧）：
 *
 * C→S: {"type":"register","hostname":"esp32p4-b","gamepad_count":2}
 * S→C: {"type":"registered","slot":1}
 * S→*: {"type":"client_joined","slot":1,"hostname":"esp32p4-b","gamepad_count":2}
 * S→*: {"type":"client_left","slot":1}
 * S→*: {"type":"slot_swap","a":1,"b":2}
 * ⇄:   {"type":"ping"} / {"type":"pong"}
 */
esp_err_t wsConsoleHandler(httpd_req_t* req);
