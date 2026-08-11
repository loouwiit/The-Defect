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

// OTA 管理（双槽 + 自动回滚可观测性）——单例封装
//
// 封壳设备无 USB 调试口，依赖 OTA 升级 + bootloader 自动回滚防变砖。
// 本模块负责：
//   - checkRollback():  启动早期检测"上次升级是否回滚"（NVS 记录）
//   - begin()/write()..:流式写入 OTA 分区（HTTP handler 调用）
//   - getInfoJson():    生成 /api/otaInfo JSON（槽位/版本/上次回滚）
//
// 回滚判定语义（精确）：
//   升级前 begin() 把目标槽 label 记入 NVS pending；
//   重启后 checkRollback() 比对 pending 与 running：
//     - pending != running → 目标槽没跑起来 → 回滚发生 → 记 last_rollback
//     - pending == running → 升级成功 → 仅清除 pending
//
// 回滚确认语义（全手动）：
//   checkRollback() 不自动 mark VALID。OTA 升级后的固件处于 PENDING_VERIFY（回滚窗口打开），
//   任何重启都会触发 bootloader 自动回退旧版。用户验证新固件正常后，
//   在 web 页面「确认此固件」→ confirmRunning() 标记 VALID 关闭窗口。
//   首次 USB 烧录的固件为 UNDEFINED（出厂信任），无需确认。
//
// ⚠ 线程安全设计（为什么单例能根治"重复创建"竞争）：
//   内部 RAM 常驻任务（xTaskCreateStatic + 静态 TCB/栈）**只能创建一次**。
//   原 C 风格实现靠"启动早期单线程显式 init + 互斥量防御"防并发重复创建；
//   本单例改用 Meyers' Singleton（C++11 magic static）——instance() 首次构造
//   由语言保证线程安全（并发调用者阻塞等待构造完成），构造函数内即创建任务/队列，
//   竞争从"手动纪律"变为"语言保证"，互斥量与防御性兜底不再需要。

class OtaManager {
public:
    static OtaManager& instance();   // Meyers' Singleton：首次调用构造（线程安全），常驻不析构

    // ── 启动早期（main.cpp 延迟 ~10s 调用一次）──
    void checkRollback();

    // 手动确认当前固件（web「确认此固件」按钮）：
    // esp_ota_mark_app_valid_cancel_rollback()，标记 VALID 关闭回滚窗口。无需重启，立即持久化。
    // ⚠ 内部会 mmap flash → 调用方须经 runInInternalStack 执行。
    esp_err_t confirmRunning();

    // ⚠ P4 硬性要求：flash mmap（esp_ota_mark_app_valid_cancel_rollback /
    //   esp_ota_get_last_invalid_partition / esp_ota_get_state_partition / esp_ota_end 内部）
    //   会 spi_flash_disable_interrupts_caches_and_other_cpu —— 禁用 flash cache 期间
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
    // 此 API 让用户手动决定下次启动哪个分区（系统还能跑 → 能开 web 页面时的兜底）：
    //   - slotLabel: "ota_0" / "ota_1"（不存在返回 ESP_ERR_NOT_FOUND）
    //   - invalidateCurrent: true = 同时把当前运行分区标记 INVALID（手动回退语义，
    //     以后 bootloader 不再自动选它；切回需再手动 setBoot）
    // 校验：目标槽非当前运行、镜像有效（esp_ota_get_partition_description）。
    // ⚠ 内部会 mmap flash → 调用方须经 runInInternalStack 执行。
    esp_err_t switchBootPartition(const char* slotLabel, bool invalidateCurrent);

    // ── 查询（/api/otaInfo）──
    // 生成 JSON 到 buffer（含 running/next/last_invalid/last_rollback/slots 状态）
    void getInfoJson(char* buffer, size_t size);

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
    // ⚠ 并发标志：6 个 HTTP coWorker 线程并发读-判-写（httpOtaInternal 内
    //   isUpdating → setUpdating(true) 非原子）。普通 bool 在 P4 双核下两个 worker
    //   可同时读到 false 双双进入 → 同时 esp_ota_begin 写同一分区 → 镜像损坏。
    //   必须用 std::atomic（test-and-set 语义由 load/store 保证）。
    std::atomic<bool> m_otaUpdating{false};
};
