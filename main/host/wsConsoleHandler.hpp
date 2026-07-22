#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

/**
 * @brief 注册跨主机管理相关的 WebSocket handlers
 *
 * 注册两个 URI：
 *   /ws/console — 管理通道（注册/槽位/心跳/广播）
 *   /ws/input   — 输入通道（从机→主机的手柄状态转发）
 *
 * 在 main.cpp 的 wsServerStart() 之后调用。
 */
bool registerHostWsHandlers();
