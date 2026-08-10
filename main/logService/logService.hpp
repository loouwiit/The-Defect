/**
 * @file logService.hpp
 * @brief LogService — 系统日志重定向服务（封壳设备远程调试）
 *
 * 通过 esp_log_set_vprintf() 接管全部 ESP_LOGx 输出：
 *   - FAT 文件持久化：/root/system/log/app.log（512KB 滚动）
 *   - WebSocket 实时推送：/ws/log（浏览器诊断页订阅）
 *   - 链式保留 UART 输出（开发板仍可看串口）
 *   - 启动早期缓冲：FAT 挂载前日志存 PSRAM，挂载后落盘
 *   - 日志级别 NVS 持久化 + 崩溃检测记录（复位原因 / panic 计数）
 *
 * 使用（main.cpp）：
 *   LogService::instance().start();        // mountFlash + nvsInit 之后
 *   wsServerRegisterWs("/ws/log", logWsHandler);  // wsServerStart 之后
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <esp_http_server.h>

class LogService {
public:
    static LogService& instance();

    /**
     * @brief 启动日志服务（幂等）
     * 注册 vprintf 回调 + 创建 LogTask（写 FAT + WS 推送）。
     * 必须在 mountFlash() 与 nvsInit() 之后调用；
     * 之前的日志进入 PSRAM 早期缓冲，启动后自动落盘。
     */
    void start();
    void stop();

    // ── HTTP API（serverKernal 调用）──

    /**
     * @brief 读取日志文件尾部 maxLines 行到 buf
     * @return 成功返回 true，buf 以 '\0' 结尾
     */
    bool readTail(char* buf, size_t bufSize, size_t maxLines);

    /** @brief 当前日志文件大小（字节） */
    size_t getFileSize() const;

    // ── 日志级别（NVS 持久化）──

    /** @brief 设置全局日志级别（0-5，对应 NONE..VERBOSE），持久化到 NVS */
    void setGlobalLevel(int level);
    /** @brief 读取全局日志级别（NVS，未设置返回 -1） */
    int getGlobalLevel() const;

    /** @brief 设置落盘阈值（0-5，默认 3=INFO）：仅级别号 ≤ 阈值的日志写入文件
     *  （数值小=更严重，如 1=仅 ERROR 落盘；2=ERROR+WARN；3=+INFO）持久化到 NVS */
    void setFileLevel(int level);
    /** @brief 当前生效的落盘阈值 */
    int getFileLevel() const;

    /** @brief 设置 WS 推送阈值（0-5，默认 3=INFO）：仅级别号 ≤ 阈值的日志推送 /ws/log */
    void setWsLevel(int level);
    /** @brief 当前生效的 WS 推送阈值 */
    int getWsLevel() const;

    // ── 崩溃检测记录（NVS）──

    /**
     * @brief 启动早期调用：检测复位原因 + coredump 是否存在，
     * 崩溃则 NVS 记录 last_crash_time / panic_count++。
     */
    void detectCrash();
    /** @brief 获取累计 panic 次数（NVS） */
    uint32_t getPanicCount() const;
    /** @brief 获取最近一次崩溃的 Unix 时间（NVS，0=无） */
    time_t getLastCrashTime() const;
    /** @brief 清除崩溃记录（NVS） */
    void clearCrashRecords();
    /** @brief 是否检测到未清除的 coredump（当前启动期评估） */
    bool hasCoredump() const;

    // ── WS 订阅（/ws/log handler 调用）──

    void addWsClient(httpd_handle_t hd, int fd);
    void removeWsClient(int fd);

    // ── vprintf 回调内部使用 ──

    /** @brief 入队一行日志（线程安全，队列满丢弃）
     *  @param level 日志级别 0-5（NONE..VERBOSE），供落盘/WS 分级过滤 */
    void enqueue(const char* line, size_t len, int level);

private:
    LogService() = default;
    ~LogService() = default;
    LogService(const LogService&) = delete;
    LogService& operator=(const LogService&) = delete;
};

/**
 * @brief /ws/log WebSocket handler（wsServerRegisterWs 注册）
 * 浏览器连接后持续接收实时日志文本帧。
 */
esp_err_t logWsHandler(httpd_req_t* req);
