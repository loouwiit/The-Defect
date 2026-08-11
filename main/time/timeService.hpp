#pragma once

#include "mutex/mutex.hpp"
#include "esp_event.h"
#include <atomic>
#include <functional>
#include <utility>
#include <vector>
#include <cstdint>
#include <ctime>
#include <sys/time.h>   // struct timeval（SNTP 同步回调参数）

/**
 * @brief 全局时间同步服务（SNTP）
 *
 * 系统级单例：启动时一次性初始化 SNTP 服务并保持运行。
 * - 自动同步默认开启，NVS 持久化（namespace "time", key "auto_sync"）
 * - WiFi 拿到 IP 时自动重新同步（GOT_IP 事件兜底，尊重自动开关）
 * - 周期性同步：lwIP SNTP 内置轮询（CONFIG_LWIP_SNTP_UPDATE_DELAY = 1h）
 * - 同步完成通知：句柄化多回调（addSyncCb / removeSyncCb），
 *   支持多个监听者 + 按句柄定点清除，App 退出时精确移除自身回调。
 *
 * 线程安全设计：
 * - 回调容器用 Mutex 保护（事件线程 / App 线程并发访问）
 * - notifySyncDone 持锁直接调用回调（不拷贝），与 removeSyncCb（App deinit 时调用）
 *   互斥 → 回调执行时注册者必然存活，无悬垂指针
 * - 约定：回调体内不得再调用本类方法（防非递归互斥锁死锁），应保持轻量
 */
class TimeService
{
public:
    using SyncCallback = std::function<void()>;
    using SyncCbHandle = uint32_t;

    static TimeService& instance();

    /** @brief 初始化 SNTP 服务（幂等）：读 NVS 自动开关、注册事件、esp_netif_sntp_init 一次 */
    void start();

    // ── 自动同步开关（NVS 持久化，默认开启）──
    bool isAutoSync() const;
    void setAutoSync(bool enable);

    // ── 手动同步 ──
    void forceSync();                 // 立即重新发起 SNTP 请求（幂等）

    // ── 状态查询 ──
    bool isSynced() const;            // 系统时间是否已同步（tm_year > 70）
    time_t getLastSyncUnix() const;   // 最近一次成功同步的 Unix 时间（0=从未）

    // ── 同步完成回调（多注册 + 定点清除）──
    SyncCbHandle addSyncCb(SyncCallback cb);
    void removeSyncCb(SyncCbHandle handle);

private:
    TimeService() = default;
    ~TimeService() = default;
    TimeService(const TimeService&) = delete;
    TimeService& operator=(const TimeService&) = delete;

    void notifySyncDone();

    // ── 回调（文件内实现，可访问私有成员）──
    static void onTimeSyncCb(struct timeval* tv);  // SNTP 同步完成回调（config.sync_cb）
    static void onGotIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

    bool m_started{ false };
    bool m_autoSync{ true };          // 默认开启，start() 时从 NVS 覆盖
    // ⚠ SNTP 任务写（onTimeSyncCb）↔ UI/HTTP 任务读（getLastSyncUnix）跨核并发；
    //   time_t 为 64 位，32 位 RISC-V 上非原子读写会撕裂（半新半旧值）→ 原子化
    std::atomic<time_t> m_lastSyncUnix{ 0 };

    mutable Mutex m_syncCbMutex;      // 保护回调容器（事件线程 / App 线程并发）
    std::vector<std::pair<SyncCbHandle, SyncCallback>> m_syncCbs;
    SyncCbHandle m_nextSyncCbHandle{ 0 }; // 从 1 开始自增分配（0 保留作无效句柄）
};
