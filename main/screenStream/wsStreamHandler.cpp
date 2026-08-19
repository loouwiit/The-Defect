/**
 * @file wsStreamHandler.cpp
 * @brief /ws/stream — 屏幕串流双任务并行编码+发送
 *
 * 编码任务：调用 ScreenStream::captureJpeg() 编码 JPEG
 * 发送任务：直接 send() 构造 WS 帧头发送
 * 两者通过 Queue 传递 buffer，真正并行。
 */

#include "wsStreamHandler.hpp"
#include "screenStream.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <task/task.hpp>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static constexpr char TAG[] = "WSStream";

static constexpr size_t kJpegBufSize = 512 * 1024;
/* 单帧发送时限：512KB 帧在拥塞 WiFi 下合法耗时可达 1-2s，超过视为失败，
 * 走「关连接 → 浏览器自动重连」自愈路径（不再静默重试 50 次导致永久死锁） */
static constexpr uint32_t kFrameDeadlineMs = 2000;

/* ============================================================
 *  双缓冲 + 任务间通信
 * ============================================================ */

EXT_RAM_BSS_ATTR static uint8_t* s_jpegBufs[2] = {};

struct SendJob {
    uint8_t* data;
    size_t   size;
};

EXT_RAM_BSS_ATTR static TaskHandle_t s_encodeTask = nullptr;
EXT_RAM_BSS_ATTR static TaskHandle_t s_sendTask = nullptr;
EXT_RAM_BSS_ATTR static QueueHandle_t s_sendQueue = nullptr;
EXT_RAM_BSS_ATTR static SemaphoreHandle_t s_freeSem = nullptr;

EXT_RAM_BSS_ATTR static int s_streamFd = -1;
EXT_RAM_BSS_ATTR static httpd_handle_t s_streamHd = nullptr;
EXT_RAM_BSS_ATTR static volatile bool s_streamActive = false;

/* pending 连接（用于客户端切换）：
 * GET handler（httpd 任务）写 → send task（帧边界，唯一消费者）读，
 * 用 portMUX 保护 hd/fd 两字段的一致性 */
struct PendingClient {
    httpd_handle_t hd;
    int fd;
};
EXT_RAM_BSS_ATTR static PendingClient s_pending = {};
EXT_RAM_BSS_ATTR static volatile bool s_hasPending = false;
static portMUX_TYPE s_pendingMux = portMUX_INITIALIZER_UNLOCKED;

/* ============================================================
 *  编码任务：捕获 JPEG → 入队
 * ============================================================ */

static void streamEncodeTask(void* arg)
{
    ESP_LOGI(TAG, "encode task started");

    while (s_streamActive && s_streamHd)
    {
        if (xSemaphoreTake(s_freeSem, pdMS_TO_TICKS(100)) != pdTRUE)
            continue;
        if (!s_streamActive || !s_streamHd) { xSemaphoreGive(s_freeSem); break; }

        static int encodeBufIdx = 0;
        int idx = encodeBufIdx;
        encodeBufIdx ^= 1;

        size_t jpegSize = ScreenStream::instance().captureJpeg(
            s_jpegBufs[idx], kJpegBufSize);
        if (jpegSize == 0)
        {
            xSemaphoreGive(s_freeSem);
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        SendJob job = { s_jpegBufs[idx], jpegSize };
        if (xQueueSend(s_sendQueue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
            xSemaphoreGive(s_freeSem);
    }

    ESP_LOGI(TAG, "encode task ended");
    s_encodeTask = nullptr;   /* 关键：任务自删前清句柄，重启门 !s_encodeTask 才有效 */
    vTaskDelete(nullptr);
}

/* ============================================================
 *  发送辅助：完整发送（部分发送 = 正常进度，阻塞超时 = 退避重试）
 * ============================================================ */

static bool sendAllBytes(int fd, const uint8_t* data, size_t len, uint32_t deadlineMs)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(deadlineMs);
    size_t off = 0;
    while (off < len)
    {
        ssize_t sent = send(fd, data + off, len - off, 0);
        if (sent > 0)
        {
            off += (size_t)sent;      /* 部分发送是正常进度，继续发送剩余字节 */
            continue;
        }
        if (sent == 0)
            return false;

        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
        {
            if (xTaskGetTickCount() >= deadline)
                return false;         /* 帧时限已到，放弃本帧 */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        ESP_LOGW(TAG, "send failed errno=%d", err);
        return false;                 /* 硬错误（EPIPE/ECONNRESET/ENOTCONN...） */
    }
    return true;
}

/* ============================================================
 *  发送任务：出队 → 构造 WS 帧头 → 完整发送
 * ============================================================ */

static void streamSendTask(void* arg)
{
    ESP_LOGI(TAG, "send task started");

    while (s_streamActive && s_streamHd)
    {
        /* 客户端切换：帧边界原子切换（本任务是唯一消费者） */
        bool switched = false;
        taskENTER_CRITICAL(&s_pendingMux);
        if (s_hasPending)
        {
            s_streamHd = s_pending.hd;
            s_streamFd = s_pending.fd;
            s_hasPending = false;
            switched = true;
        }
        taskEXIT_CRITICAL(&s_pendingMux);
        if (switched)
        {
            ESP_LOGI(TAG, "stream client switched to fd=%d", s_streamFd);
            /* 收紧新 fd 的发送超时（默认 5s → 1s），快速失败快速重连 */
            struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
            setsockopt(s_streamFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }

        SendJob job;
        if (xQueueReceive(s_sendQueue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
            continue;
        if (!s_streamActive || !s_streamHd) { xSemaphoreGive(s_freeSem); break; }

        if (httpd_ws_get_fd_info(s_streamHd, s_streamFd) != HTTPD_WS_CLIENT_WEBSOCKET)
        {
            ESP_LOGI(TAG, "stream client disconnected");
            s_streamActive = false;   /* 断连也停管线，否则 encode 永不退出 → 重启门被卡死 */
            xSemaphoreGive(s_freeSem);
            break;
        }

        /* 构造 WS 二进制帧头 (0x82 = FIN + binary) */
        uint8_t header[10];
        header[0] = 0x82;
        size_t headerSize;
        uint64_t n = job.size;
        if (n < 126) {
            header[1] = (uint8_t)n; headerSize = 2;
        } else if (n < 65536) {
            header[1] = 126;
            header[2] = (uint8_t)(n >> 8); header[3] = (uint8_t)n;
            headerSize = 4;
        } else {
            header[1] = 127;
            header[2] = (uint8_t)(n >> 56); header[3] = (uint8_t)(n >> 48);
            header[4] = (uint8_t)(n >> 40); header[5] = (uint8_t)(n >> 32);
            header[6] = (uint8_t)(n >> 24); header[7] = (uint8_t)(n >> 16);
            header[8] = (uint8_t)(n >> 8);  header[9] = (uint8_t)n;
            headerSize = 10;
        }

        bool ok = sendAllBytes(s_streamFd, header, headerSize, kFrameDeadlineMs);
        if (ok)
            ok = sendAllBytes(s_streamFd, job.data, job.size, kFrameDeadlineMs);

        xSemaphoreGive(s_freeSem);   /* 无论成败都归还缓冲 */

        if (!ok)
        {
            /* 帧发送失败：主动关闭连接让浏览器 onclose → 5s 自动重连（全新流），
             * 不再静默累计 50 次错误后自杀导致流永久死亡 */
            ESP_LOGW(TAG, "frame send failed (size=%u), closing stream client", (unsigned)job.size);
            httpd_sess_trigger_close(s_streamHd, s_streamFd);
            s_streamActive = false;
            break;
        }
    }

    ESP_LOGI(TAG, "send task ended");

    /* 退出清理：drain 队列归还缓冲，让 encode task 尽快退出 */
    SendJob dummy;
    while (s_sendQueue && xQueueReceive(s_sendQueue, &dummy, 0) == pdTRUE)
        xSemaphoreGive(s_freeSem);

    s_sendTask = nullptr;   /* 关键：任务自删前清句柄，重启门 !s_sendTask 才有效 */
    vTaskDelete(nullptr);
}

/* ============================================================
 *  启动 / 停止双任务
 * ============================================================ */

static bool startStreamTasks()
{
    /* 等待旧 encode task 完全退出（send 已清句柄，encode 可能仍在收尾）——
     * 必须在删除 sem/queue 之前，否则 take 已删除对象 = 未定义行为 */
    int wait = 100;   /* 最多 ~1s：encode 最坏路径 = take 超时 100ms + 编码 500ms + 入队 100ms */
    while (s_encodeTask && --wait)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (s_encodeTask)
    {
        ESP_LOGW(TAG, "old encode task still alive, skip start");
        return false;
    }

    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_DMA;
    if (!s_jpegBufs[0]) s_jpegBufs[0] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
    if (!s_jpegBufs[1]) s_jpegBufs[1] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
    if (!s_jpegBufs[0] || !s_jpegBufs[1]) return false;

    /* 每次重建信号量/队列：保证计数与队列状态干净（防上一轮计数泄漏漂移） */
    if (s_freeSem)   { vSemaphoreDelete(s_freeSem);   s_freeSem   = nullptr; }
    if (s_sendQueue) { vQueueDelete(s_sendQueue);    s_sendQueue = nullptr; }
    s_freeSem   = xSemaphoreCreateCounting(2, 2);
    s_sendQueue = xQueueCreate(2, sizeof(SendJob));
    if (!s_freeSem || !s_sendQueue) return false;

    /* 收紧串流 fd 的发送超时（默认 5s → 1s），快速失败快速重连 */
    if (s_streamFd >= 0)
    {
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(s_streamFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    s_streamActive = true;

    if (xTaskCreate(streamEncodeTask, "wsEnc", 4096, nullptr,
            Task::Priority::Deamon, &s_encodeTask) != pdPASS)
        { s_streamActive = false; return false; }

    if (xTaskCreate(streamSendTask, "wsSend", 4096, nullptr,
            Task::Priority::Deamon, &s_sendTask) != pdPASS)
        { s_streamActive = false; return false; }

    return true;
}

/* ============================================================
 *  WebSocket handler — 屏幕串流
 * ============================================================ */

esp_err_t wsStreamHandler(httpd_req_t* req)
{
    if (req->method == HTTP_GET)
    {
        /* ⚠ 校验 WebSocket 握手头：普通浏览器直接 GET /ws/stream（非 WS 握手）若挂
         *  pending，send task 采纳后因 fd 非 WEBSOCKET 会停掉正在进行的活跃串流。 */
        char upgrade[16] = "";
        if (httpd_req_get_hdr_value_str(req, "Upgrade", upgrade, sizeof(upgrade)) != ESP_OK ||
            strncasecmp(upgrade, "websocket", 9) != 0)
        {
            ESP_LOGW(TAG, "reject non-WS GET /ws/stream");
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "WebSocket only");
            return ESP_OK;
        }

        /* 新客户端一律挂 pending，由 send task 在帧边界原子切换，
         * 不再粗暴杀旧管线（旧逻辑让活跃流直接停摆） */
        taskENTER_CRITICAL(&s_pendingMux);
        s_pending.hd = req->handle;
        s_pending.fd = httpd_req_to_sockfd(req);
        s_hasPending = true;
        taskEXIT_CRITICAL(&s_pendingMux);
        ESP_LOGI(TAG, "ws/stream handshake done (fd=%d), queued as pending", s_pending.fd);
        return ESP_OK;
    }

    /* 丢弃客户端请求帧 */
    {
        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
        if (ret != ESP_OK) return ret;
        if (pkt.len > 0)
        {
            uint8_t* tmp = (uint8_t*)heap_caps_malloc(pkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (tmp)
            {
                pkt.payload = tmp;
                httpd_ws_recv_frame(req, &pkt, pkt.len);
                heap_caps_free(tmp);
            }
        }
    }

    /* 启动双任务（任务句柄退出时已清空，此处重启门恢复有效；
     * 若任务仍在跑则跳过——send task 会在帧边界采纳 pending 新客户端） */
    if (!s_encodeTask && !s_sendTask)
    {
        taskENTER_CRITICAL(&s_pendingMux);
        if (s_hasPending)
        {
            s_streamHd = s_pending.hd;
            s_streamFd = s_pending.fd;
            s_hasPending = false;
        }
        taskEXIT_CRITICAL(&s_pendingMux);

        if (s_streamHd && s_streamFd >= 0)
        {
            if (!startStreamTasks())
            {
                s_streamActive = false;
                ESP_LOGE(TAG, "start stream tasks failed");
            }
        }
    }

    return ESP_OK;
}

/* ============================================================
 *  wsServer 广播隔离：判断 fd 是否为当前串流客户端
 * ============================================================ */

bool wsStreamIsFd(int fd)
{
    return fd >= 0 && fd == s_streamFd;
}
