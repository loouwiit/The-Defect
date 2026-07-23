#pragma once

#include <esp_http_server.h>

/**
 * @brief /ws/input WebSocket handler
 *
 * 接收 10 字节二进制 GamepadState 帧：
 *   [0]    localPlayer — 从机上的手柄编号 (0-3)
 *   [1-2]  buttons — uint16 小端序
 *   [3]    lx
 *   [4]    ly
 *   [5]    rx
 *   [6]    ry
 *   [7]    lt
 *   [8]    rt
 *   [9]    dpad
 *
 * 通过 InputHost 查询 fd → hostId，
 * 然后注入 VirtualGamepad::inject(hostId, localPlayer, state)。
 */
esp_err_t wsInputHandler(httpd_req_t* req);
