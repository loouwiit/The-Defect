#pragma once

#include <esp_http_server.h>

/**
 * @brief /ws/stream WebSocket handler
 *
 * 屏幕串流（MJPEG）WebSocket 入口。
 * 客户端连接后发送任意 binary 帧触发双任务并行编码+发送。
 * 内部使用 ScreenStream::captureJpeg() + 硬件 JPEG 编码器。
 *
 * 在 wsServerStart() 后调用 wsServerRegisterWs() 注册。
 */
esp_err_t wsStreamHandler(httpd_req_t* req);

/**
 * @brief 判断指定 fd 是否为当前串流客户端
 *
 * 供 wsServerBroadcastText 广播时排除串流专用通道，
 * 避免 httpd 任务与 streamSendTask 并发写同一 socket 造成帧交错。
 */
bool wsStreamIsFd(int fd);
