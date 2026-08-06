#pragma once

#include "display/display.hpp"
#include "touch/touch.hpp"
#include "mutex/mutex.hpp"
#include "task/task.hpp"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

/**
 * @brief GT911 五 indev 多指桥接层
 *
 * 中断驱动读取（非 60fps 轮询）：
 *   GT911 INT(GPIO47) → ISR 给二进制信号量 → 读线程（Touch::update 读全 5 点，含 track_id）
 *   → track_id→槽位路由 → 每根手指转发到独立 lv_indev（POINTER）。
 *   LVGL 每 indev 是独立指针状态机 → 多指同时按下各自发 PRESSED/PRESSING/RELEASED，
 *   所有 App 零改动获得多指 LVGL 事件。
 *
 * release 握手（VirtualIndev 的 hasNewData 模式推广到 5 槽）：
 *   槽的 RELEASED 被 LVGL read_cb 消费前，不得呈现新按压/坐标（暂存 pendingPress），
 *   杜绝 track_id 复用导致的"未释放却跳坐标"→ PRESS_LOST/假点击。
 */
class GT911Indev
{
public:
    static GT911Indev& instance();

    bool start(Display* display, Touch* touch);
    void stop();

    // 调试/可视化：读取各槽位状态（TestApp 试验台用）
    struct SlotInfo
    {
        bool active{};       // 槽是否绑定手指
        bool pressed{};      // 面向 LVGL 的按下状态
        int8_t trackId{-1};  // 绑定 track_id
        lv_point_t point{};  // 当前坐标（LVGL 屏幕坐标）
    };
    void getSlots(SlotInfo* out, size_t max) const;

    // 槽位数（与 Touch::MaxPointCount 一致），供 App / 渲染器引用
    static constexpr int kSlotCount = 5;

private:
    static constexpr char TAG[] = "GT911Indev";
    static constexpr int kTrackIdRange = 256; // GT911 track_id 为 1 字节

    GT911Indev();
    ~GT911Indev() = default;

    Display* m_display{};
    Touch* m_touch{};
    bool m_started{};
    volatile bool m_running{};

    lv_indev_t* m_indevs[kSlotCount]{};

    // ── track_id → 槽位路由 ──
    int8_t m_slotOfTrackId[kTrackIdRange];    // track_id → slot，-1 = 未分配

    struct Slot
    {
        int8_t trackId = -1;        // 绑定 track_id，-1 = 空闲
        lv_point_t point{0, 0};     // 面向 LVGL 的坐标（最后呈现）
        bool pressed = false;       // 面向 LVGL 的按下状态
        bool hasNewData = false;    // 有状态变化待 LVGL 消费（VirtualIndev 同款）
        bool pendingPress = false;  // release 未消费时暂存的按压
        lv_point_t pendingPoint{0, 0};
    } m_slots[kSlotCount];

    mutable Mutex m_mutex;          // 保护路由表 + 槽快照（读线程 ↔ LVGL read_cb）
    SemaphoreHandle_t m_sem{};
    Thread m_thread;

    int findFreeSlot() const;
    void updateAndRoute();

    static void threadMain(void* param);
    static void isrCb(esp_lcd_touch_handle_t tp);
    static void indevReadCb(lv_indev_t* drv, lv_indev_data_t* data);
};
