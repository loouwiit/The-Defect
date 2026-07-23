#pragma once

#include <functional>
#include <cstdint>
#include <cstddef>
#include <esp_websocket_client.h>

/**
 * @brief 通用 WebSocket 客户端（薄封装）
 *
 * 基于 esp_websocket_client，提供简洁 API。
 * 回调直接在 WS Task 上下文中触发，调用者负责异步调度。
 *
 * 使用方式：
 *   WsClient client;
 *   client.setOnMessage([](const char* data, size_t len) {
 *       ESP_LOGI(TAG, "收到: %.*s", len, data);
 *   });
 *   client.connect("192.168.1.42", 8080, "/ws/test");
 *   client.sendText("hello", 5);
 *   // ...
 *   client.disconnect();
 */
class WsClient {
public:
    WsClient();
    ~WsClient();

    // 禁止拷贝
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;

    /// 连接远程 WebSocket 服务器。uri 如 "/ws/test"
    bool connect(const char* host, uint16_t port, const char* uri);

    /// 断开连接
    void disconnect();

    /// 是否已连接
    bool isConnected() const;

    /// 发送 UTF-8 文本帧
    bool sendText(const char* data, size_t len);

    // ── 事件回调（WS Task 上下文中触发）──

    /// 收到文本帧
    void setOnMessage(std::function<void(const char* data, size_t len)> cb) {
        m_onMessage = std::move(cb);
    }

    /// 连接建立
    void setOnConnected(std::function<void()> cb) {
        m_onConnected = std::move(cb);
    }

    /// 连接断开
    void setOnDisconnected(std::function<void()> cb) {
        m_onDisconnected = std::move(cb);
    }

private:
    static void eventHandler(void* arg, esp_event_base_t base,
                             int32_t eventId, void* eventData);
    void handleEvent(int32_t eventId, esp_websocket_event_data_t* data);

    esp_websocket_client* m_client = nullptr;
    std::function<void(const char* data, size_t len)> m_onMessage;
    std::function<void()> m_onConnected;
    std::function<void()> m_onDisconnected;
};
