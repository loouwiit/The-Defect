/**
 * @file logService.cpp
 * @brief LogService — 系统日志重定向服务实现
 *
 * 架构：
 *   ESP_LOGx → esp_log_set_vprintf(logVprintf)
 *     ├─ vsnprintf 格式化 → 行缓冲
 *     ├─ xQueueSend 入队（64×256B，满则丢弃，绝不阻塞调用方）
 *     └─ 链式调用旧 handler（保留 UART 输出）
 *
 *   LogTask（独立任务，低优先级）：
 *     ├─ 启动时 dump 早期缓冲（FAT 挂载前日志）→ app.log
 *     ├─ 出队 → fwrite 追加到 app.log（攒批 4KB / 200ms flush）
 *     ├─ 文件 >512KB → 滚动（app.log.prev）
 *     └─ 每行同步推送到 /ws/log 订阅客户端
 *
 * 并发安全：
 *   - vprintf 回调可被多任务并发调用（文档要求可重入）：
 *     vsnprintf 线程安全，xQueueSend 线程安全，早期缓冲用 portMUX
 *   - 回调内不做任何文件 IO / 动态分配，全部移入 LogTask
 */

#include "logService.hpp"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#include <nvs.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>

#include "task/task.hpp"
#include "websocket/wsServer.hpp"

static constexpr char TAG[] = "LogSvc";

/* ============================================================
 *  常量
 * ============================================================ */

static constexpr size_t   kLineMax       = 256;          // 单行最大长度（含 '\0'）
static constexpr size_t   kQueueDepth    = 64;           // 行队列深度
static constexpr size_t   kFlushBytes    = 4096;         // 攒批写入阈值（字节）
static constexpr uint32_t kFlushMs       = 200;          // 定时 flush 周期
static constexpr size_t   kMaxFileSize   = 512 * 1024;   // 滚动阈值（512KB）
static constexpr char     kLogDir[]      = "/root/system/log";
static constexpr char     kLogPath[]     = "/root/system/log/app.log";
static constexpr char     kLogPrevPath[] = "/root/system/log/app.log.prev";
static constexpr size_t   kEarlyBufSize  = 16 * 1024;    // 早期缓冲（FAT 挂载前）
static constexpr int      kMaxWsClients  = 4;            // /ws/log 订阅上限

static constexpr char     kNvsNamespace[] = "log";
static constexpr char     kNvsKeyLevel[]  = "global_level";
static constexpr char     kNvsKeyFileLevel[] = "file_level";   // 落盘阈值
static constexpr char     kNvsKeyWsLevel[]   = "ws_level";     // WS 推送阈值
static constexpr char     kNvsKeyPanic[]  = "panic_count";
static constexpr char     kNvsKeyCrash[]  = "last_crash";

/* 按 tag 源头过滤持久化：固定槽位（NVS 无动态列表），每槽 tag 名 + 级别 */
static constexpr size_t   kMaxTagOverrides = 8;
static constexpr size_t   kMaxTagLen       = 32;
static constexpr size_t   kMaxTagPrefixLen = 6;   // NVS key 前缀最大长度（"taglv_"=6，"tag_"=4）
static constexpr char     kNvsKeyTagCount[] = "tag_count";
static constexpr char     kNvsKeyTagPrefix[] = "tag_";      // tag_0..7（str）
static constexpr char     kNvsKeyTagLvlPrefix[] = "taglv_"; // taglv_0..7（i32）

/* 默认阈值：INFO(3)。与旧行为一致（全部消费级别 ≤3 的日志：E/W/I）。
 * 语义沿用 esp_log：级别号越小越严重（NONE=0, ERROR=1, WARN=2, INFO=3, DEBUG=4, VERBOSE=5），
 * “保留”条件 = level ≤ 阈值。例如 file_level=1 → 仅 ERROR 落盘；ws_level=3 → WS 显示 INFO+。 */
static constexpr int      kDefaultLevel    = 3;

/* ============================================================
 *  全局状态
 * ============================================================ */

EXT_RAM_BSS_ATTR static vprintf_like_t    s_oldVprintf = nullptr;
EXT_RAM_BSS_ATTR static QueueHandle_t     s_queue      = nullptr;
EXT_RAM_BSS_ATTR static TaskHandle_t      s_task       = nullptr;
EXT_RAM_BSS_ATTR static volatile bool     s_running    = false;

EXT_RAM_BSS_ATTR static FILE*             s_file       = nullptr;
EXT_RAM_BSS_ATTR static size_t            s_fileSize   = 0;

/* 早期缓冲（FAT 挂载前日志，逐行 '\0' 分隔） */
EXT_RAM_BSS_ATTR static char              s_earlyBuf[kEarlyBufSize];
EXT_RAM_BSS_ATTR static volatile uint32_t s_earlyPos  = 0;
static portMUX_TYPE s_earlyMux = portMUX_INITIALIZER_UNLOCKED;

/* /ws/log 订阅集合 */
EXT_RAM_BSS_ATTR static httpd_handle_t    s_wsHd   = nullptr;
EXT_RAM_BSS_ATTR static int               s_wsFds[kMaxWsClients];
static portMUX_TYPE s_wsMux = portMUX_INITIALIZER_UNLOCKED;

/* ⚠ LogTask 静态栈必须放内部 RAM（普通 static → .bss）：
 * LogTask 每 200ms 调 fsync → FatFs f_sync → 写 flash 期间
 * spi_flash_disable_interrupts_caches_and_other_cpu 禁用 cache →
 * PSRAM 栈不可访问 = 崩（曾实测 49s 后 LogTask 静默死亡，日志停写）。
 * 与 OtaManager/ElfManager 同款规则。勿加 EXT_RAM_BSS_ATTR。 */
static StaticTask_t s_logTcb;
static StackType_t  s_logStack[4096];   // 内部 RAM（fwrite→FatFs→flash 写调用链深）

/* 诊断计数（LogTask 自检日志用） */
EXT_RAM_BSS_ATTR static volatile uint32_t s_writeCount   = 0;  // 成功写入行数
EXT_RAM_BSS_ATTR static volatile uint32_t s_writeBytes   = 0;  // 成功写入字节
EXT_RAM_BSS_ATTR static volatile uint32_t s_errCount     = 0;  // 文件/fsync 错误次数
EXT_RAM_BSS_ATTR static volatile uint32_t s_dropCount    = 0;  // 队列满丢弃行数

/* 生效的分级阈值（setter 更新 + NVS 持久化；LogTask 读取做分流） */
EXT_RAM_BSS_ATTR static volatile int s_fileLevel = kDefaultLevel;
EXT_RAM_BSS_ATTR static volatile int s_wsLevel   = kDefaultLevel;

/* 队列项：行内容 + 级别（0-5，用于落盘/WS 分级过滤） */
struct LogItem {
    char    line[kLineMax];
    uint8_t level;
};

static int nvsReadLevel(const char* key);   // 定义在下方（start() 早于定义，需前置声明）

static void nvsTagKey(char* out, size_t outSize, const char* prefix, size_t idx)
{
    // ⚠ %.*s 限定 prefix 精度 + idx 取模：GCC O2 下 -Wformat-truncation 对未知长度
    // 字符串做最坏推断（strlen 可达数百字节）→ 恒误报截断。实际输出最长
    // 6(prefix) + 1(idx<8，取模后单十进制位) = 7 字节 < 16，精度约束后编译器可证明不截断
    snprintf(out, outSize, "%.*s%u", (int)kMaxTagPrefixLen, prefix, (unsigned)(idx % 10));
}

/* ============================================================
 *  文件操作（LogTask 内调用）
 * ============================================================ */

static size_t fileSizeOf(const char* path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return (size_t)st.st_size;
    return 0;
}

static void openLogFile()
{
    if (s_file) return;
    mkdir(kLogDir, 0755);
    s_file = fopen(kLogPath, "a");
    if (!s_file)
    {
        s_errCount = s_errCount + 1;
        ESP_LOGE(TAG, "fopen %s failed (err=%d)", kLogPath, errno);
        return;
    }
    /* 用 lseek END 取实际大小（不依赖 stat——目录项可能滞后于 f_sync） */
    if (fseek(s_file, 0, SEEK_END) == 0)
        s_fileSize = (size_t)ftell(s_file);
    else
        s_fileSize = 0;
}

static void flushLogFile()
{
    if (!s_file) return;
    if (fflush(s_file) != 0)
    {
        s_errCount = s_errCount + 1;
        ESP_LOGE(TAG, "fflush failed (err=%d)", errno);
    }
    /* ⚠ 关键：fflush 只刷 newlib stdio 缓冲，不更新 FAT 目录项中的文件大小——
     * FatFs 的 f_write 只改内存中文件对象的 fsize，目录项大小须 f_sync（fsync）才写回。
     * 若只 fflush，stat()/ftell() 恒返回旧值（新文件=0），/api/log 会读到空。 */
    if (fsync(fileno(s_file)) != 0)
    {
        s_errCount = s_errCount + 1;
        ESP_LOGE(TAG, "fsync failed (err=%d)", errno);
    }
}

static void closeLogFile()
{
    if (s_file)
    {
        fclose(s_file);
        s_file = nullptr;
    }
}

/** 文件超过阈值 → 滚动：app.log → app.log.prev，新建 app.log */
static void rotateLogFile()
{
    if (s_fileSize <= kMaxFileSize) return;
    closeLogFile();
    remove(kLogPrevPath);
    rename(kLogPath, kLogPrevPath);
    s_fileSize = 0;
    openLogFile();
}

static void appendToFile(const char* line, size_t len)
{
    if (!s_file) openLogFile();
    if (!s_file)
    {
        s_errCount = s_errCount + 1;   // FAT 未就绪/打开失败，本行丢弃（下轮重试）
        return;
    }
    if (fwrite(line, 1, len, s_file) != len)
    {
        // 写失败（如 FAT 被格式化/卸载/磁盘满）→ 关闭句柄，下轮重开
        s_errCount = s_errCount + 1;
        ESP_LOGE(TAG, "fwrite failed (err=%d)", errno);
        closeLogFile();
        return;
    }
    s_fileSize += len;
    s_writeCount = s_writeCount + 1;
    s_writeBytes = s_writeBytes + (uint32_t)len;
}

/** dump 早期缓冲（FAT 挂载前的日志）到文件头部 */
static void dumpEarlyToFile()
{
    uint32_t pos;
    portENTER_CRITICAL(&s_earlyMux);
    pos = s_earlyPos;
    portEXIT_CRITICAL(&s_earlyMux);
    if (pos == 0) return;

    openLogFile();
    if (s_file)
    {
        fwrite(s_earlyBuf, 1, pos, s_file);
        s_fileSize = fileSizeOf(kLogPath);
        flushLogFile();
    }
    portENTER_CRITICAL(&s_earlyMux);
    s_earlyPos = 0;
    portEXIT_CRITICAL(&s_earlyMux);
}

/* ============================================================
 *  WS 订阅推送（LogTask 内调用）
 * ============================================================ */

void LogService::addWsClient(httpd_handle_t hd, int fd)
{
    portENTER_CRITICAL(&s_wsMux);
    s_wsHd = hd;
    for (int i = 0; i < kMaxWsClients; i++)
    {
        if (s_wsFds[i] == fd)
        {
            portEXIT_CRITICAL(&s_wsMux);
            return;
        }
        if (s_wsFds[i] < 0)
        {
            s_wsFds[i] = fd;
            portEXIT_CRITICAL(&s_wsMux);
            ESP_LOGI(TAG, "ws/log client subscribed fd=%d", fd);
            return;
        }
    }
    portEXIT_CRITICAL(&s_wsMux);
    ESP_LOGW(TAG, "ws/log client full (fd=%d dropped)", fd);
}

void LogService::removeWsClient(int fd)
{
    portENTER_CRITICAL(&s_wsMux);
    for (int i = 0; i < kMaxWsClients; i++)
    {
        if (s_wsFds[i] == fd)
            s_wsFds[i] = -1;
    }
    portEXIT_CRITICAL(&s_wsMux);
}

static void pushToWsClients(const char* line, size_t len)
{
    int fds[kMaxWsClients];
    httpd_handle_t hd;

    portENTER_CRITICAL(&s_wsMux);
    hd = s_wsHd;
    memcpy(fds, s_wsFds, sizeof(fds));
    portEXIT_CRITICAL(&s_wsMux);

    if (!hd) return;

    for (int i = 0; i < kMaxWsClients; i++)
    {
        if (fds[i] < 0) continue;
        /* 断连检测：非 websocket 状态 → 移除订阅 */
        if (httpd_ws_get_fd_info(hd, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
        {
            LogService::instance().removeWsClient(fds[i]);
            continue;
        }
        wsServerSendText(fds[i], line, len);
    }
}

/* ============================================================
 *  LogTask — 出队 → 写文件 + WS 推送
 * ============================================================ */

static void logTask(void* arg)
{
    /* ⚠ 这条日志会入队自己，被下方循环收到后写入文件（无害） */
    ESP_LOGI(TAG, "log task started (internal RAM stack)");
    dumpEarlyToFile();

    TickType_t lastFlush = xTaskGetTickCount();
    TickType_t lastStatus = xTaskGetTickCount();
    size_t pendingBytes = 0;

    while (s_running)
    {
        LogItem item;
        if (xQueueReceive(s_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            size_t len = strlen(item.line);
            /* 分级分流：级别号 ≤ 阈值才保留（数值小=更严重）
             * 落盘与 WS 独立过滤 → 可“仅 error 落盘、WS 显示 info” */
            if (item.level <= (uint8_t)s_fileLevel)
            {
                appendToFile(item.line, len);
                pendingBytes += len;
            }
            if (item.level <= (uint8_t)s_wsLevel)
                pushToWsClients(item.line, len);
        }
        /* 攒批 flush：4KB 或 200ms（仅在有新写入时，避免空闲期空 fsync 磨损 FAT） */
        TickType_t now = xTaskGetTickCount();
        if (pendingBytes >= kFlushBytes ||
            (pendingBytes > 0 && (now - lastFlush) >= pdMS_TO_TICKS(kFlushMs)))
        {
            flushLogFile();
            lastFlush = now;
            pendingBytes = 0;
            rotateLogFile();
        }

        /* 周期自检（每 10s）：写入了多少 / 错误数 / 队列深度 —— 排障关键段 */
        if ((now - lastStatus) >= pdMS_TO_TICKS(10000))
        {
            lastStatus = now;
            ESP_LOGI(TAG, "status: lines=%u bytes=%u err=%u drop=%u file=%u queue=%u free=%u fileLvl=%d wsLvl=%d",
                     (unsigned)s_writeCount, (unsigned)s_writeBytes,
                     (unsigned)s_errCount, (unsigned)s_dropCount,
                     (unsigned)fileSizeOf(kLogPath),
                     (unsigned)uxQueueMessagesWaiting(s_queue),
                     (unsigned)(kQueueDepth - uxQueueMessagesWaiting(s_queue)),
                     (int)s_fileLevel, (int)s_wsLevel);
        }
    }

    flushLogFile();
    closeLogFile();
    s_task = nullptr;
    ESP_LOGI(TAG, "log task ended");
    vTaskDelete(nullptr);
}

/* ============================================================
 *  vprintf 回调（可重入，多任务并发调用）
 * ============================================================ */

/* 从格式串解析日志级别：ESP_LOGx 宏展开后 fmt 以级别字母开头
 * （CONFIG_LOG_COLORS 开启时前面有 \x1b[..m ANSI 转义，需跳过）。
 * 返回 0-5；解析失败返回 0（=全部保留，避免误过滤）。 */
static int parseLogLevel(const char* fmt)
{
    const char* p = fmt;
    if (p && p[0] == '\x1b')
    {
        while (*p && *p != 'm') p++;
        if (*p == 'm') p++;
    }
    switch (*p)
    {
        case 'N': return 0;   // NONE
        case 'E': return 1;   // ERROR
        case 'W': return 2;   // WARN
        case 'I': return 3;   // INFO
        case 'D': return 4;   // DEBUG
        case 'V': return 5;   // VERBOSE
        default:  return 0;   // 未知 → 全保留
    }
}

static int logVprintf(const char* fmt, va_list args)
{
    char buf[kLineMax];

    /* va_list 只能消费一次：拷贝一份给 vsnprintf，原份留给旧 handler */
    va_list argsCopy;
    va_copy(argsCopy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, argsCopy);
    va_end(argsCopy);

    if (len > 0)
    {
        if (len > (int)sizeof(buf) - 1)
            len = (int)sizeof(buf) - 1;
        LogService::instance().enqueue(buf, (size_t)len, parseLogLevel(fmt));
    }

    /* 链式调用旧 handler（保留 UART 输出） */
    if (s_oldVprintf)
        return s_oldVprintf(fmt, args);
    return len;
}

/* ============================================================
 *  LogService 实现
 * ============================================================ */

LogService& LogService::instance()
{
    static LogService inst;
    return inst;
}

void LogService::start()
{
    if (s_running) return;

    /* 先建队列再注册 vprintf：注册后日志走队列，早期缓冲不再写入 */
    s_queue = xQueueCreate(kQueueDepth, sizeof(LogItem));
    if (!s_queue)
    {
        ESP_LOGE(TAG, "xQueueCreate failed");
        return;
    }
    for (int i = 0; i < kMaxWsClients; i++)
        s_wsFds[i] = -1;

    /* 恢复持久化的全局 + 分级阈值（在注册 vprintf 前，LogTask 启动即生效） */
    {
        int level = getGlobalLevel();
        if (level >= 0 && level <= 5)
            esp_log_level_set("*", (esp_log_level_t)level);
        int fl = nvsReadLevel(kNvsKeyFileLevel);
        if (fl >= 0 && fl <= 5) s_fileLevel = fl;
        int wl = nvsReadLevel(kNvsKeyWsLevel);
        if (wl >= 0 && wl <= 5) s_wsLevel = wl;

        /* 恢复按 tag 源头过滤（esp_log_level_set 本身不持久化） */
        restoreTagLevels();
    }

    s_oldVprintf = esp_log_set_vprintf(logVprintf);
    s_running = true;

    /* ⚠ 必须 xTaskCreateStatic + 内部 RAM 静态栈（见 s_logStack 注释）：
     * 普通 xTaskCreate 栈落 PSRAM，写 flash 禁 cache 期间访问 = 崩 */
    s_task = xTaskCreateStatic(logTask, "logTask",
                               sizeof(s_logStack) / sizeof(StackType_t),
                               nullptr, Task::Priority::Deamon,
                               s_logStack, &s_logTcb);
    if (s_task == nullptr)
    {
        s_running = false;
        ESP_LOGE(TAG, "xTaskCreateStatic failed");
    }
}

void LogService::stop()
{
    s_running = false;
}

void LogService::enqueue(const char* line, size_t len, int level)
{
    if (s_queue)
    {
        /* 队列满 → 丢弃新行（绝不阻塞调用方） */
        LogItem item;
        item.level = (uint8_t)(level < 0 ? 0 : (level > 5 ? 5 : level));
        if (len >= kLineMax) len = kLineMax - 1;
        memcpy(item.line, line, len);
        item.line[len] = '\0';
        if (xQueueSend(s_queue, &item, 0) != pdTRUE)
            s_dropCount = s_dropCount + 1;   // 日志风暴时保系统稳定
    }
    else
    {
        /* 早期缓冲（FAT 挂载前 / start() 前） */
        portENTER_CRITICAL(&s_earlyMux);
        uint32_t pos = s_earlyPos;
        if (pos + len + 1 <= kEarlyBufSize)
        {
            memcpy(s_earlyBuf + pos, line, len);
            s_earlyBuf[pos + len] = '\0';
            s_earlyPos = pos + len + 1;
        }
        /* 缓冲满 → 丢弃（启动早期日志量小，可接受） */
        portEXIT_CRITICAL(&s_earlyMux);
    }
}

/* ============================================================
 *  HTTP API 支撑
 * ============================================================ */

bool LogService::readTail(char* buf, size_t bufSize, size_t maxLines)
{
    if (!buf || bufSize == 0) return false;

    /* 打开文件，用 fseek/ftell 取实际大小（不依赖 stat——目录项大小可能滞后于
     * LogTask 写入，fsync 已保证 ≤200ms 内更新，这里再兜底） */
    FILE* f = fopen(kLogPath, "r");
    if (!f)
    {
        /* 文件不存在：退回返回早期缓冲（启动早期 /api/log 查询） */
        uint32_t pos;
        portENTER_CRITICAL(&s_earlyMux);
        pos = s_earlyPos;
        portEXIT_CRITICAL(&s_earlyMux);
        if (pos == 0) { buf[0] = '\0'; return true; }

        /* ⚠ 早期缓冲以 '\0' 分隔行（enqueue 写入 s_earlyBuf[pos+len]='\0'，非 '\n'）——
         *   原按 '\n' 查找导致 maxLines 截断失效（整段被当一行）+ 崩溃 backtrace
         *   内含 '\n' 时误切行。此处按 '\0' 对齐写入侧。 */
        size_t start = 0, nl = 0;
        for (long i = (long)pos - 1; i >= 0 && nl < maxLines; i--)
        {
            if (s_earlyBuf[i] == '\0') { nl++; start = (size_t)i + 1; }
        }
        size_t len = pos - start;
        if (len > bufSize - 1) { start += len - (bufSize - 1); len = bufSize - 1; }
        memcpy(buf, s_earlyBuf + start, len);
        buf[len] = '\0';
        return true;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0)
    {
        fclose(f);
        buf[0] = '\0';
        return true;
    }

    /* 读整个文件（≤512KB，PSRAM 充足），从尾部数 maxLines 行 */
    size_t fileSize = (size_t)sz;
    char* tmp = (char*)heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp)
    {
        fclose(f);
        buf[0] = '\0';   // 分配失败也保证安全空串
        return false;
    }
    size_t got = fread(tmp, 1, fileSize, f);
    fclose(f);
    tmp[got] = '\0';

    size_t start = 0, nl = 0;
    for (long i = (long)got - 1; i >= 0 && nl < maxLines; i--)
    {
        if (tmp[i] == '\n') { nl++; start = (size_t)i + 1; }
    }
    size_t len = got - start;
    if (len > bufSize - 1) { start += len - (bufSize - 1); len = bufSize - 1; }
    memcpy(buf, tmp + start, len);
    buf[len] = '\0';
    heap_caps_free(tmp);
    return true;
}

size_t LogService::getFileSize() const
{
    return fileSizeOf(kLogPath);
}

/* ============================================================
 *  日志级别（NVS 持久化）
 * ============================================================ */

static int nvsReadLevel(const char* key)
{
    nvs_handle_t h;
    int32_t v = -1;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_i32(h, key, &v);
        nvs_close(h);
    }
    return (int)v;
}

static void nvsWriteLevel(const char* key, int level)
{
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_i32(h, key, level);
        nvs_commit(h);
        nvs_close(h);
    }
}

void LogService::setGlobalLevel(int level)
{
    if (level < 0 || level > 5) return;
    esp_log_level_set("*", (esp_log_level_t)level);
    nvsWriteLevel(kNvsKeyLevel, level);
}

int LogService::getGlobalLevel() const
{
    return nvsReadLevel(kNvsKeyLevel);
}

void LogService::setFileLevel(int level)
{
    if (level < 0 || level > 5) return;
    s_fileLevel = level;          // 立即生效（LogTask 读该变量分流）
    nvsWriteLevel(kNvsKeyFileLevel, level);
    ESP_LOGI(TAG, "file_level=%d (仅更严重日志落盘)", level);
}

int LogService::getFileLevel() const
{
    return (int)s_fileLevel;
}

void LogService::setWsLevel(int level)
{
    if (level < 0 || level > 5) return;
    s_wsLevel = level;
    nvsWriteLevel(kNvsKeyWsLevel, level);
    ESP_LOGI(TAG, "ws_level=%d (仅更严重日志推送 WS)", level);
}

int LogService::getWsLevel() const
{
    return (int)s_wsLevel;
}

/* ============================================================
 *  按 tag 源头过滤（esp_log_level_set，vprintf 之前生效）
 * ============================================================ */

void LogService::setTagLevel(const char* tag, int level)
{
    if (!tag || !tag[0] || level < 0 || level > 5) return;

    /* 立即生效（esp_log 内部源头筛选，低于级别的日志不再进入 vprintf） */
    esp_log_level_set(tag, (esp_log_level_t)level);

    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;

    /* 已存在 → 更新；否则找空槽/覆盖最旧（固定 8 槽） */
    int32_t count = 0;
    nvs_get_i32(h, kNvsKeyTagCount, &count);
    if (count < 0) count = 0;
    if (count > (int32_t)kMaxTagOverrides) count = kMaxTagOverrides;

    char key[16];
    bool updated = false;
    for (int32_t i = 0; i < count && !updated; i++)
    {
        nvsTagKey(key, sizeof(key), kNvsKeyTagPrefix, (size_t)i);
        char existing[kMaxTagLen];
        size_t len = sizeof(existing);
        if (nvs_get_str(h, key, existing, &len) == ESP_OK && strcmp(existing, tag) == 0)
        {
            nvsTagKey(key, sizeof(key), kNvsKeyTagLvlPrefix, (size_t)i);
            nvs_set_i32(h, key, level);
            nvs_commit(h);
            updated = true;
        }
    }

    if (!updated)
    {
        /* 槽位满 → 覆盖最后一个（简单策略） */
        int32_t slot = (count < (int32_t)kMaxTagOverrides) ? count : (int32_t)kMaxTagOverrides - 1;
        nvsTagKey(key, sizeof(key), kNvsKeyTagPrefix, (size_t)slot);
        nvs_set_str(h, key, tag);
        nvsTagKey(key, sizeof(key), kNvsKeyTagLvlPrefix, (size_t)slot);
        nvs_set_i32(h, key, level);
        if (count < (int32_t)kMaxTagOverrides)
            nvs_set_i32(h, kNvsKeyTagCount, count + 1);
        nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "tag_level[%s]=%d", tag, level);
}

void LogService::clearTagLevel(const char* tag)
{
    if (!tag || !tag[0]) return;
    // ⚠ 恢复为当前全局级别而非 ESP_LOG_NONE：esp_log 中 tag 级别优先于 "*" 级别，
    //   置 NONE 会让该 tag 日志彻底静默（即使全局级别允许也不产生），
    //   与"清除 tag 过滤"语义不符。注意：清除后再改全局级别不会联动本 tag
    //   （esp_log 固有覆盖语义，可接受）。
    int global = getGlobalLevel();
    esp_log_level_set(tag, (global >= 0 && global <= 5) ? (esp_log_level_t)global : ESP_LOG_INFO);

    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;

    int32_t count = 0;
    nvs_get_i32(h, kNvsKeyTagCount, &count);
    if (count < 0) count = 0;
    if (count > (int32_t)kMaxTagOverrides) count = kMaxTagOverrides;

    char key[16];
    for (int32_t i = 0; i < count; i++)
    {
        nvsTagKey(key, sizeof(key), kNvsKeyTagPrefix, (size_t)i);
        char existing[kMaxTagLen];
        size_t len = sizeof(existing);
        if (nvs_get_str(h, key, existing, &len) == ESP_OK && strcmp(existing, tag) == 0)
        {
            /* 后面槽位前移，保持紧凑 */
            for (int32_t j = i; j < count - 1; j++)
            {
                nvsTagKey(key, sizeof(key), kNvsKeyTagPrefix, (size_t)(j + 1));
                char nextTag[kMaxTagLen];
                size_t nextLen = sizeof(nextTag);
                if (nvs_get_str(h, key, nextTag, &nextLen) == ESP_OK)
                {
                    char k2[16];
                    nvsTagKey(k2, sizeof(k2), kNvsKeyTagPrefix, (size_t)j);
                    nvs_set_str(h, k2, nextTag);
                    nvsTagKey(k2, sizeof(k2), kNvsKeyTagLvlPrefix, (size_t)j);
                    char kl[16];
                    nvsTagKey(kl, sizeof(kl), kNvsKeyTagLvlPrefix, (size_t)(j + 1));
                    int32_t lv = 0;
                    if (nvs_get_i32(h, kl, &lv) == ESP_OK) nvs_set_i32(h, k2, lv);
                }
            }
            nvs_set_i32(h, kNvsKeyTagCount, count - 1);
            nvs_commit(h);
            break;
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "tag_level[%s] cleared", tag);
}

int LogService::getTagLevel(const char* tag) const
{
    if (!tag) return -1;
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return -1;

    int32_t count = 0;
    nvs_get_i32(h, kNvsKeyTagCount, &count);
    if (count < 0) count = 0;
    if (count > (int32_t)kMaxTagOverrides) count = kMaxTagOverrides;

    int result = -1;
    char key[16];
    for (int32_t i = 0; i < count; i++)
    {
        nvsTagKey(key, sizeof(key), kNvsKeyTagPrefix, (size_t)i);
        char existing[kMaxTagLen];
        size_t len = sizeof(existing);
        if (nvs_get_str(h, key, existing, &len) == ESP_OK && strcmp(existing, tag) == 0)
        {
            nvsTagKey(key, sizeof(key), kNvsKeyTagLvlPrefix, (size_t)i);
            int32_t lv = -1;
            nvs_get_i32(h, key, &lv);
            result = (int)lv;
            break;
        }
    }
    nvs_close(h);
    return result;
}

size_t LogService::getTagLevelCount() const
{
    nvs_handle_t h;
    int32_t count = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_i32(h, kNvsKeyTagCount, &count);
        nvs_close(h);
    }
    if (count < 0) return 0;
    if (count > (int32_t)kMaxTagOverrides) return kMaxTagOverrides;
    return (size_t)count;
}

bool LogService::getTagLevelAt(size_t i, char* tagOut, size_t tagOutSize, int* levelOut) const
{
    if (i >= getTagLevelCount() || !tagOut || tagOutSize == 0) return false;
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return false;

    char key[16];
    nvsTagKey(key, sizeof(key), kNvsKeyTagPrefix, i);
    size_t len = tagOutSize;
    esp_err_t err = nvs_get_str(h, key, tagOut, &len);
    nvsTagKey(key, sizeof(key), kNvsKeyTagLvlPrefix, i);
    int32_t lv = -1;
    nvs_get_i32(h, key, &lv);
    nvs_close(h);
    if (err != ESP_OK || lv < 0 || lv > 5) return false;
    if (levelOut) *levelOut = (int)lv;
    return true;
}

/* 启动恢复：遍历 NVS 槽位，逐个 esp_log_level_set（在 vprintf 注册前调用） */
void LogService::restoreTagLevels()
{
    size_t count = getTagLevelCount();
    for (size_t i = 0; i < count; i++)
    {
        char tag[kMaxTagLen];
        int level = -1;
        if (getTagLevelAt(i, tag, sizeof(tag), &level))
            esp_log_level_set(tag, (esp_log_level_t)level);
    }
}

/* ============================================================
 *  崩溃检测记录（NVS）
 * ============================================================ */

void LogService::detectCrash()
{
    uint32_t panicCount = getPanicCount();
    time_t now = time(nullptr);

    /* 复位原因判断 */
    esp_reset_reason_t reason = esp_reset_reason();
    bool crashed = (reason == ESP_RST_PANIC ||
                    reason == ESP_RST_INT_WDT ||
                    reason == ESP_RST_TASK_WDT ||
                    reason == ESP_RST_BROWNOUT);

    /* coredump 是否存在（崩溃现场待下载） */
    bool hasDump = false;
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (part)
    {
        uint32_t check = 0xFFFFFFFF;
        if (esp_partition_read(part, 0, &check, sizeof(check)) == ESP_OK)
            hasDump = (check != 0xFFFFFFFF);
    }

    if (crashed)
    {
        panicCount++;
        nvs_handle_t h;
        if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK)
        {
            nvs_set_u32(h, kNvsKeyPanic, panicCount);
            nvs_set_i64(h, kNvsKeyCrash, (int64_t)now);
            nvs_commit(h);
            nvs_close(h);
        }
        ESP_LOGE(TAG, "detected crash (reason=%d), panic_count=%u, has_coredump=%d",
                 (int)reason, (unsigned)panicCount, (int)hasDump);
    }
    else if (hasDump)
    {
        /* 上次崩溃留下的 coredump 未被清除（非崩溃启动也会遇到） */
        ESP_LOGW(TAG, "coredump present (previous crash), download via /api/coredump");
    }
}

uint32_t LogService::getPanicCount() const
{
    nvs_handle_t h;
    uint32_t count = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_u32(h, kNvsKeyPanic, &count);
        nvs_close(h);
    }
    return count;
}

time_t LogService::getLastCrashTime() const
{
    nvs_handle_t h;
    int64_t t = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_i64(h, kNvsKeyCrash, &t);
        nvs_close(h);
    }
    return (time_t)t;
}

void LogService::clearCrashRecords()
{
    nvs_handle_t h;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_u32(h, kNvsKeyPanic, 0);
        nvs_set_i64(h, kNvsKeyCrash, 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool LogService::hasCoredump() const
{
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
    if (!part) return false;
    uint32_t check = 0xFFFFFFFF;
    if (esp_partition_read(part, 0, &check, sizeof(check)) != ESP_OK)
        return false;
    return check != 0xFFFFFFFF;
}

/* ============================================================
 *  /ws/log WebSocket handler
 * ============================================================ */

esp_err_t logWsHandler(httpd_req_t* req)
{
    if (req->method == HTTP_GET)
    {
        LogService::instance().addWsClient(req->handle, httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    /* 丢弃后续帧（保持连接活跃，断连由 LogTask 推送时检测） */
    httpd_ws_frame_t pkt = {};
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
    return ESP_OK;
}
