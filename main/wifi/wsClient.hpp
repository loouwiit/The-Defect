#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/**
 * @brief 通用 WebSocket 客户端封装
 *
 * 封装 esp_websocket_client，提供消息队列 + 事件回调。
 * 线程模型：后台 Task 接收 WS 数据 → Queue → update() 消费。
 *
 * 使用方式：
 *   WsClient client;
 *   client.setOnMessage([](const char* data, size_t len) {
 *       ESP_LOGI(TAG, "收到: %.*s", len, data);
 *   });
 *   client.connect("192.168.1.42", 8080, "/ws/tetris");
 *   // 每帧调用:
 *   client.update();
 *   client.sendText("hello", 5);
 */
class WsClient {
public:
    WsClient();
    ~WsClient();

    // 非拷贝、非移动
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    // ── 生命周期 ──
    bool connect(const char* host, uint16_t port, const char* uri);
    void disconnect();
    bool isConnected() const;

    // ── 发送 ──
    bool sendText(const char* data, size_t len);

    // ── 事件回调 ──
    void setOnMessage(std::function<void(const char* data, size_t len)> cb);
    void setOnConnected(std::function<void()> cb);
    void setOnDisconnected(std::function<void()> cb);

    // ── 每帧调用（从消息队列消费，触发回调）──
    void update();

    // ── 获取内部句柄（供 HostManager 查询 fd 等）──
    int getFd() const;

    // ── 内部实现（公开供 wsEventHandler 访问）──
    struct Impl;
    static constexpr size_t MSG_QUEUE_LEN = 16;
    static constexpr size_t MSG_MAX_LEN   = 4096;

private:
    Impl* m_impl{};
};
