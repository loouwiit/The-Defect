#pragma once

#include <esp_http_server.h>

/**
 * @brief /ws/test WebSocket handler
 *
 * 收到文本帧 → 打印日志 → 回复 "echo: <原文>"
 * 用于验证跨主机 WebSocket 通信能力。
 */
esp_err_t testWsHandler(httpd_req_t* req);
