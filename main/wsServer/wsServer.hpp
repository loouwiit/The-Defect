#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

/**
 * @brief WebSocket 处理函数类型
 * 和 httpd_uri_t 的 handler 签名一致
 */
typedef esp_err_t (*WsHandlerFn)(httpd_req_t*);

// ============================================================
//  生命周期
// ============================================================

bool wsServerStart();
void wsServerStop();

// ============================================================
//  注册 / 注销（供各游戏模块调用）
// ============================================================

/**
 * @brief 注册 WebSocket URI handler
 * @param uri  路径，如 "/ws/tetris"
 * @param handler 处理函数
 * @return true 成功
 *
 * 各游戏模块在 init 时调用此函数注册自己的 handler，
 * deinit 时调用 wsServerUnregister 注销。
 */
bool wsServerRegisterWs(const char* uri, WsHandlerFn handler);

/**
 * @brief 注销 WebSocket URI handler
 * @param uri  之前注册的路径
 */
void wsServerUnregister(const char* uri);

// ============================================================
//  消息发送
// ============================================================

/**
 * @brief 发送 UTF-8 文本帧到指定客户端
 * @param fd   客户端 socket fd
 * @param data UTF-8 文本数据
 * @param len  数据长度（不含 \0）
 * @return ESP_OK 成功
 */
esp_err_t wsServerSendText(int fd, const char* data, size_t len);

/**
 * @brief 广播 UTF-8 文本帧到所有 WebSocket 客户端
 * @param data UTF-8 文本数据
 * @param len  数据长度
 * @return ESP_OK 成功
 */
esp_err_t wsServerBroadcastText(const char* data, size_t len);
