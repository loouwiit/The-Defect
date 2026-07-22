#include "wsClient.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>
#include <cstdio>

static constexpr char TAG[] = "WsClient";

// ============================================================
//  WebSocket 帧 helpers（裸 socket，无 esp_websocket_client）
// ============================================================

/// 发送 HTTP Upgrade 请求并等待 101 响应
static bool wsHttpUpgrade(int fd, const char* host, uint16_t port, const char* uri)
{
    char req[512];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        uri, host, port);
    if (n <= 0) return false;

    if (send(fd, req, n, 0) != n) return false;

    char resp[256];
    int r = recv(fd, resp, sizeof(resp) - 1, 0);
    if (r <= 0) return false;
    resp[r] = '\0';
    return strstr(resp, "101") != nullptr;
}

/// 发送 WebSocket 文本帧（客户端→服务器，必须 masking）
static bool wsSendTextFrame(int fd, const char* data, size_t len)
{
    ESP_LOGD(TAG, "send %u bytes", (unsigned)len);
    uint8_t mask[4];
    mask[0] = (uint8_t)esp_random();
    mask[1] = (uint8_t)esp_random();
    mask[2] = (uint8_t)esp_random();
    mask[3] = (uint8_t)esp_random();

    uint8_t hdr[14];
    size_t hs;
    hdr[0] = 0x81;  // FIN + text opcode

    if (len < 126) {
        hdr[1] = 0x80 | (uint8_t)len;
        memcpy(hdr + 2, mask, 4);
        hs = 6;
    } else if (len < 65536) {
        hdr[1] = 0xFE;
        hdr[2] = (uint8_t)(len >> 8);
        hdr[3] = (uint8_t)(len);
        memcpy(hdr + 4, mask, 4);
        hs = 8;
    } else {
        hdr[1] = 0xFF;
        uint64_t n64 = len;
        for (int i = 0; i < 8; i++) hdr[2 + i] = (uint8_t)(n64 >> (56 - i * 8));
        memcpy(hdr + 10, mask, 4);
        hs = 14;
    }

    if (send(fd, hdr, hs, 0) != (ssize_t)hs) return false;

    // 逐块 masking + 发送
    for (size_t i = 0; i < len; ) {
        uint8_t buf[512];
        size_t n = (len - i < sizeof(buf)) ? (len - i) : sizeof(buf);
        for (size_t j = 0; j < n; j++)
            buf[j] = (uint8_t)data[i + j] ^ mask[(i + j) & 3];
        if (send(fd, buf, n, 0) != (ssize_t)n) return false;
        i += n;
    }
    return true;
}

// ============================================================
//  接收 Task（独立 Task，从 socket 读取 WS 帧）
// ============================================================

struct RecvCtx {
    int fd;
    QueueHandle_t queue;
    std::function<void()> onDisc;
};

static void recvTask(void* arg)
{
    auto* ctx = static_cast<RecvCtx*>(arg);
    constexpr size_t BUF_SIZE = WsClient::MSG_MAX_LEN;
    auto* buf = (uint8_t*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { delete ctx; vTaskDelete(nullptr); return; }

    while (true) {
        int r = recv(ctx->fd, buf, BUF_SIZE, 0);
        if (r <= 0) break;

        // dump 前 32 字节看实际 WS 帧格式
        if (r > 0) {
            char hex[128] = {};
            for (int i = 0; i < (r < 32 ? r : 32); i++)
                snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", buf[i]);
            ESP_LOGI(TAG, "recv %d bytes, head: %s", r, hex);
        }

        // 简单 WS 帧解析：提取 text(op=1) 和 binary(op=2) 帧的 payload
        size_t pos = 0;
        while ((size_t)r > pos) {
            uint8_t opcode = buf[pos] & 0x0F;
            if (opcode != 1 && opcode != 2) { pos++; continue; }
            pos++;  // 跳过 opcode
            if (pos >= (size_t)r) break;

            uint64_t payLen;
            if (buf[pos] < 126) {
                payLen = buf[pos] & 0x7F;
                pos++;
            } else if (buf[pos] == 126) {
                if (pos + 3 > (size_t)r) break;
                payLen = ((uint64_t)buf[pos+1] << 8) | buf[pos+2];
                pos += 3;
            } else {
                if (pos + 9 > (size_t)r) break;
                payLen = 0;
                for (int i = 0; i < 8; i++) payLen = (payLen << 8) | buf[pos+1+i];
                pos += 9;
            }
            // 服务端帧无 masking
            if (pos + payLen > (size_t)r) break;

            size_t copyLen = (payLen < WsClient::MSG_MAX_LEN) ? (size_t)payLen : WsClient::MSG_MAX_LEN - 1;
            char* msg = (char*)heap_caps_malloc(copyLen + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (msg) {
                memcpy(msg, buf + pos, copyLen);
                msg[copyLen] = '\0';
                ESP_LOGI(TAG, "queue msg: %.*s", (int)copyLen, msg);
                xQueueSend(ctx->queue, &msg, 0);
            }
            pos += payLen;
        }
    }

    heap_caps_free(buf);
    if (ctx->onDisc) ctx->onDisc();
    close(ctx->fd);
    delete ctx;
    vTaskDelete(nullptr);
}

// ============================================================
//  内部实现
// ============================================================

struct WsClient::Impl {
    int fd = -1;
    QueueHandle_t msgQueue = nullptr;
    TaskHandle_t recvTask = nullptr;
    volatile bool connected = false;

    std::function<void(const char*, size_t)> onMessage;
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
};

WsClient::WsClient()
    : m_impl(new Impl())
{
    m_impl->msgQueue = xQueueCreate(MSG_QUEUE_LEN, sizeof(char*));
}

WsClient::~WsClient()
{
    disconnect();
    if (m_impl) {
        if (m_impl->msgQueue) {
            char* p;
            while (xQueueReceive(m_impl->msgQueue, &p, 0) == pdTRUE)
                heap_caps_free(p);
            vQueueDelete(m_impl->msgQueue);
        }
        delete m_impl;
    }
}

bool WsClient::connect(const char* host, uint16_t port, const char* uri)
{
    if (!m_impl || m_impl->fd >= 0) return false;

    // DNS
    struct addrinfo hints = {}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return false;

    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); return false; }

    if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd); freeaddrinfo(res); return false;
    }
    freeaddrinfo(res);

    if (!wsHttpUpgrade(fd, host, port, uri)) {
        close(fd);
        return false;
    }

    m_impl->fd = fd;
    m_impl->connected = true;

    // 接收 Task
    auto* ctx = new RecvCtx{
        fd, m_impl->msgQueue,
        [this]() { m_impl->connected = false;
                   if (m_impl->onDisconnected) m_impl->onDisconnected(); }
    };
    if (xTaskCreate(recvTask, "wsR", 6144, ctx, 5, &m_impl->recvTask) != pdPASS) {
        close(fd); m_impl->fd = -1; m_impl->connected = false;
        delete ctx;
        return false;
    }

    ESP_LOGI(TAG, "connected");
    if (m_impl->onConnected) m_impl->onConnected();
    return true;
}

void WsClient::disconnect()
{
    if (!m_impl || m_impl->fd < 0) return;

    if (m_impl->recvTask) {
        vTaskDelete(m_impl->recvTask);
        m_impl->recvTask = nullptr;
    }
    close(m_impl->fd);
    m_impl->fd = -1;
    m_impl->connected = false;

    char* p;
    while (xQueueReceive(m_impl->msgQueue, &p, 0) == pdTRUE)
        heap_caps_free(p);
}

bool WsClient::isConnected() const { return m_impl && m_impl->connected; }

bool WsClient::sendText(const char* data, size_t len)
{
    if (!isConnected()) return false;
    return wsSendTextFrame(m_impl->fd, data, len);
}

void WsClient::setOnMessage(std::function<void(const char*, size_t)> cb)
{ if (m_impl) m_impl->onMessage = std::move(cb); }

void WsClient::setOnConnected(std::function<void()> cb)
{ if (m_impl) m_impl->onConnected = std::move(cb); }

void WsClient::setOnDisconnected(std::function<void()> cb)
{ if (m_impl) m_impl->onDisconnected = std::move(cb); }

void WsClient::update()
{
    if (!m_impl || !m_impl->msgQueue || !m_impl->onMessage) return;
    char* p;
    while (xQueueReceive(m_impl->msgQueue, &p, 0) == pdTRUE && p) {
        m_impl->onMessage(p, strlen(p));
        heap_caps_free(p);
    }
}

int WsClient::getFd() const { return m_impl ? m_impl->fd : -1; }
