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
#include <sys/socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static constexpr char TAG[] = "WSStream";

static constexpr size_t kJpegBufSize = 512 * 1024;
static constexpr int kMaxConsecutiveErrors = 50;

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

/* pending 连接（用于客户端切换） */
EXT_RAM_BSS_ATTR static httpd_handle_t s_pendingHd = nullptr;
EXT_RAM_BSS_ATTR static int s_pendingFd = -1;
EXT_RAM_BSS_ATTR static volatile bool s_hasPending = false;

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
    vTaskDelete(nullptr);
}

/* ============================================================
 *  发送任务：出队 → 构造 WS 帧头 → send()
 * ============================================================ */

static void streamSendTask(void* arg)
{
    ESP_LOGI(TAG, "send task started");
    int consecutiveErrors = 0;

    while (s_streamActive && s_streamHd)
    {
        SendJob job;
        if (xQueueReceive(s_sendQueue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
            continue;
        if (!s_streamActive || !s_streamHd) { xSemaphoreGive(s_freeSem); break; }

        if (httpd_ws_get_fd_info(s_streamHd, s_streamFd) != HTTPD_WS_CLIENT_WEBSOCKET)
        {
            ESP_LOGI(TAG, "stream client disconnected");
            xSemaphoreGive(s_freeSem);
            break;
        }

        // 构造 WS 二进制帧头 (0x82 = FIN + binary)
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

        ssize_t sent = send(s_streamFd, header, headerSize, 0);
        if (sent <= 0 || (size_t)sent != headerSize) {
            consecutiveErrors++;
            if (consecutiveErrors >= kMaxConsecutiveErrors) { xSemaphoreGive(s_freeSem); break; }
            xSemaphoreGive(s_freeSem);
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }

        sent = send(s_streamFd, job.data, job.size, 0);
        if (sent <= 0 || (size_t)sent != job.size) {
            consecutiveErrors++;
            if (consecutiveErrors >= kMaxConsecutiveErrors) { xSemaphoreGive(s_freeSem); break; }
            xSemaphoreGive(s_freeSem);
            vTaskDelay(pdMS_TO_TICKS(33));
            continue;
        }
        consecutiveErrors = 0;

        xSemaphoreGive(s_freeSem);
    }

    ESP_LOGI(TAG, "send task ended");
    vTaskDelete(nullptr);
}

/* ============================================================
 *  启动 / 停止双任务
 * ============================================================ */

static bool startStreamTasks()
{
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_DMA;
    if (!s_jpegBufs[0]) s_jpegBufs[0] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
    if (!s_jpegBufs[1]) s_jpegBufs[1] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
    if (!s_jpegBufs[0] || !s_jpegBufs[1]) return false;

    if (!s_freeSem)  s_freeSem  = xSemaphoreCreateCounting(2, 2);
    if (!s_sendQueue) s_sendQueue = xQueueCreate(2, sizeof(SendJob));
    if (!s_freeSem || !s_sendQueue) return false;

    s_streamActive = true;

    if (xTaskCreate(streamEncodeTask, "wsEnc", 4096, nullptr,
            Task::Priority::Deamon, &s_encodeTask) != pdPASS)
        { s_streamActive = false; return false; }

    if (xTaskCreate(streamSendTask, "wsSend", 4096, nullptr,
            Task::Priority::Deamon, &s_sendTask) != pdPASS)
        { s_streamActive = false; return false; }

    return true;
}

static void stopStreamTasks()
{
    s_streamActive = false;
    int wait = 150;
    while ((s_encodeTask || s_sendTask) && --wait)
        vTaskDelay(pdMS_TO_TICKS(10));
    s_encodeTask = s_sendTask = nullptr;

    SendJob dummy;
    while (s_sendQueue && xQueueReceive(s_sendQueue, &dummy, 0) == pdTRUE)
        xSemaphoreGive(s_freeSem);
}

/* ============================================================
 *  WebSocket handler — 屏幕串流
 * ============================================================ */

esp_err_t wsStreamHandler(httpd_req_t* req)
{
    if (req->method == HTTP_GET)
    {
        if (s_streamActive || s_encodeTask || s_sendTask)
        {
            ESP_LOGW(TAG, "old stream client present, queuing new as pending");
            // 异步踢掉旧客户端
            s_streamActive = false;
            s_pendingHd = req->handle;
            s_pendingFd = httpd_req_to_sockfd(req);
            s_hasPending = true;
        }
        else
        {
            s_streamHd = req->handle;
            s_streamFd = httpd_req_to_sockfd(req);
            s_streamActive = false;
            s_hasPending = false;
        }
        ESP_LOGI(TAG, "ws/stream handshake done (fd=%d)", httpd_req_to_sockfd(req));
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

    /* 启动双任务 */
    if (!s_encodeTask && !s_sendTask)
    {
        if (s_hasPending)
        {
            s_streamHd = s_pendingHd;
            s_streamFd = s_pendingFd;
            s_hasPending = false;
            s_streamActive = true;
        }

        if (!startStreamTasks())
        {
            s_streamActive = false;
            ESP_LOGE(TAG, "start stream tasks failed");
        }
    }

    return ESP_OK;
}
