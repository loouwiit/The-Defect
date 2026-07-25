#pragma once

#include <esp_http_server.h>

/**
 * @brief /ws/console WebSocket handler
 *
 * 管理通道，处理设备注册/心跳/全量槽位广播。
 * 协议（JSON 文本帧）：
 *
 * C→S: {"type":"register","hostname":"esp32p4-b","gamepad_count":2}
 * S→C: {"type":"registered","slot":1}
 * S→*: {"type":"slots_update","slots":[
 *         {"slot":0,"hostname":"esp32p4-a","connected":true,"gamepad_count":0},
 *         {"slot":1,"hostname":"esp32p4-b","connected":true,"gamepad_count":2},
 *         {"slot":2,"hostname":"","connected":false,"gamepad_count":0},
 *         {"slot":3,"hostname":"","connected":false,"gamepad_count":0}
 *       ]}
 * ⇄:   {"type":"ping"} / {"type":"pong"}
 */
esp_err_t wsConsoleHandler(httpd_req_t* req);
