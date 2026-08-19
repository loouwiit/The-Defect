#pragma once
#include <esp_err.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <atomic>
#include <esp_partition.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// OTA 管理（双槽 + 自动回滚）——单例封装
//
// 封壳设备依赖 OTA 升级 + bootloader 自动回滚防变砖。
// 回滚判定：升级前 begin() 记目标槽到 NVS，重启后 checkRollback() 比对 pending 与 running。
// 回滚确认：OTA 升级后固件处于 PENDING_VERIFY，用户验证正常后 web 页面调 confirmRunning() 关闭窗口。

class OtaManager {
public:
    static OtaManager& instance();   // Meyers' Singleton：首次调用构造（线程安全），常驻不析构

    // ── 启动早期（main.cpp 延迟 ~10s 调用一次）──
    void checkRollback();

    // 手动确认当前固件：标记 VALID 关闭回滚窗口
    // 内部会 mmap flash → 调用方须经 runInInternalStack 执行
    esp_err_t confirmRunning();

    // P4 硬性要求：flash mmap 会禁用 cache，调用方须经 runInInternalStack 执行
    //   调用任务栈必须在**内部 RAM**（PSRAM 依赖 cache，禁用即不可访问，assert 崩溃）。
    //   而 Task::addTask daemon 栈与 server coWorker 栈均为 PSRAM（new[] → CONFIG_SPIRAM_USE_MALLOC）。
    //   因此所有 OTA API 调用必须经 runInInternalStack 在内部 RAM 栈任务中执行（阻塞等待完成）。
    void runInInternalStack(void (*fn)(void*), void* arg);

    // 内部 RAM 静态缓冲（httpOta 使用，见实现注释）：
    //   esp_ota_write 写 flash 禁 cache 窗口内源缓冲必须在内部 RAM；静态 .bss 避免堆损坏/栈溢出。
    //   经 runInInternalStack 互斥，同一时间仅一个使用者。
    uint8_t* getWriteBuffer();    // 4096 字节
    uint8_t* getHeaderBuffer();   // HeaderSize 字节

    // ── OTA 并发标志（HTTP handler 防双 worker 同时升级）──
    bool isUpdating();
    void setUpdating(bool updating);

    // ── 升级前置检查 ──
    // 镜像预检所需读取的最小头部字节数（覆盖 image header + app_desc，供 HTTP handler 读第一块）
    static constexpr size_t HeaderSize = 512;

    // 获取下一个可写 OTA 分区（无则 nullptr）
    const void* getNextUpdatePartition();

    // 从已读到的镜像头预检：magic 校验 + 版本防重烧（与 last_invalid 同版本拒绝）。
    // header: 至少覆盖 app_desc 的镜像头数据；headerLen: 实际读到的字节数。
    // 返回 ESP_OK 通过；其他错误见 esp_err_to_name。
    esp_err_t precheckImage(const uint8_t* header, size_t headerLen);

    // 目标槽剩余可写大小（上传镜像超过则拒绝）
    size_t getUpdatePartitionSize();

    // 开始 OTA（记录 pending 到 NVS），成功后返回 ESP_OK + handle
    esp_err_t begin(void** outHandle);

    // 流式写入一块
    esp_err_t write(void* handle, const uint8_t* data, size_t len);

    // 写入出错时中止
    void abort(void* handle);

    // 写入完成：校验镜像 + 设置启动分区。成功返回 ESP_OK
    esp_err_t finish(void* handle);

    // ── 手动分区切换 / 回退（web 页面「分区管理」用）──
    // 10s 自动回滚只保护启动早期崩溃；固件被确认 VALID 后运行中崩溃不会自动回退。
    // 手动切换下次启动分区（运行中崩溃兜底）
    // 内部会 mmap flash → 调用方须经 runInInternalStack 执行
    esp_err_t switchBootPartition(const char* slotLabel, bool invalidateCurrent);

    // ── 查询（/api/otaInfo）──
    // 生成 JSON 到 buffer（含 running/next/last_invalid/last_rollback/slots 状态）
    void getInfoJson(char* buffer, size_t size);

    // ── 启动弹窗结果（checkRollback 后供 main.cpp 调用 showOtaResultPopup）──
    struct OtaResult {
        bool hasEvent{};          // 本次启动有 OTA 事件（升级成功或回滚）
        bool rolledBack{};        // 回滚发生（目标槽没跑起来）
        bool pendingVerify{};     // 当前固件待确认（PENDING_VERIFY）
        char slot[32]{};          // 当前运行槽 label
        char version[32]{};       // 当前运行版本
        char pendingSlot[32]{};   // 上次升级目标槽（回滚时填充）
    };
    const OtaResult& getOtaResult() const { return m_otaResult; }

private:
    OtaManager();                // 私有构造：创建内部 RAM 常驻任务 + 请求队列
    ~OtaManager() = default;     // 单例常驻，永不析构

    struct InternalStackCtx
    {
        void (*fn)(void*);
        void* arg;
        SemaphoreHandle_t done;
    };

    static void internalStackWorker(void* p);   // 常驻任务入口（经 xTaskCreateStatic 注册）

    // ── 状态（单例实例在函数局部 static → .bss 内部 RAM，勿加 EXT_RAM_BSS_ATTR）──
    StackType_t m_otaIntStack[8192];   // 内部 RAM 静态栈（worker 用）
    StaticTask_t m_otaIntTcb;
    TaskHandle_t m_otaIntHandle{};
    QueueHandle_t m_otaIntQueue{};     // 请求队列（runInInternalStack → worker）

    const esp_partition_t* m_updatePartition{};
    // 并发标志：httpOtaInternal 经 runInInternalStack（队列深度 1）串行执行，
    // 故 isUpdating 的读-判-写序列实际不会并发（安全由队列串行化保证）
    std::atomic<bool> m_otaUpdating{false};

    // checkRollback 结果（供 main.cpp 弹窗用）
    OtaResult m_otaResult{};
};
