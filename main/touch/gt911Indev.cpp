#include "touch/gt911Indev.hpp"
#include "esp_lcd_touch.h"

GT911Indev& GT911Indev::instance()
{
    static GT911Indev inst;
    return inst;
}

GT911Indev::GT911Indev()
{
    // track_id → 槽位路由表初始化为"未分配"
    for (auto& s : m_slotOfTrackId)
        s = -1;
}

bool GT911Indev::start(Display* display, Touch* touch)
{
    if (m_started) return true;
    if (!display || !touch || !touch->getHandle()) return false;

    m_display = display;
    m_touch = touch;

    // ── 注册 5 个独立 POINTER indev（display.start() 之前，与 adapter 注册时机一致）──
    {
        auto guard = display->lockGuard();
        for (int i = 0; i < kSlotCount; i++)
        {
            lv_indev_t* indev = lv_indev_create();
            if (!indev)
            {
                ESP_LOGE(TAG, "lv_indev_create failed (slot %d)", i);
                continue;
            }
            lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(indev, display->getLvglDisplay());
            lv_indev_set_read_cb(indev, indevReadCb);
            lv_indev_set_driver_data(indev, (void*)(intptr_t)i);
            m_indevs[i] = indev;
        }
    }

    // ── IRQ：二进制信号量 + 组件触摸中断回调（镜像 adapter IRQ 模式）──
    m_sem = xSemaphoreCreateBinary();
    if (!m_sem)
    {
        ESP_LOGE(TAG, "failed to create semaphore");
        return false;
    }
    esp_err_t err = esp_lcd_touch_register_interrupt_callback_with_data(
        touch->getHandle(), isrCb, (void*)m_sem);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "register touch interrupt callback failed: %s", esp_err_to_name(err));
        return false;
    }

    // ── 读线程 ──
    m_running = true;
    m_thread = Thread{ threadMain, "gt911Indev", this, Thread::Priority::Normal, 4096 };

    m_started = true;
    ESP_LOGI(TAG, "GT911Indev started, %d indevs (INT driven)", kSlotCount);
    return true;
}

void GT911Indev::stop()
{
    if (!m_started) return;
    m_running = false;
    if (m_sem) xSemaphoreGive(m_sem);   // 唤醒线程退出
    m_started = false;
}

void GT911Indev::threadMain(void* param)
{
    auto& self = *static_cast<GT911Indev*>(param);

    xSemaphoreGive(self.m_sem);   // 初始读一次（镜像 adapter IRQ 模式的初始 give）

    while (self.m_running)
    {
        // INT 唤醒；30ms 超时兜底（GT911 INT 在手指移动时未必每帧触发，保证移动刷新）
        xSemaphoreTake(self.m_sem, pdMS_TO_TICKS(30));
        self.updateAndRoute();
    }
}

void GT911Indev::isrCb(esp_lcd_touch_handle_t tp)
{
    auto sem = static_cast<SemaphoreHandle_t>(tp->config.user_data);
    if (!sem) return;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(sem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

int GT911Indev::findFreeSlot() const
{
    for (int s = 0; s < kSlotCount; s++)
        if (m_slots[s].trackId == -1)
            return s;
    return -1;
}

void GT911Indev::updateAndRoute()
{
    if (!m_touch || !m_touch->update())
        return;

    const uint8_t count = m_touch->getCount();
    Lock lock(m_mutex);

    bool seen[kSlotCount] = { false };

    for (uint8_t i = 0; i < count; i++)
    {
        const Touch::PointData p = (*m_touch)[i];
        const uint8_t tid = p.track_id;
        if (tid >= kTrackIdRange) continue;   // 防御

        const lv_point_t pt{ (lv_coord_t)p.x, (lv_coord_t)p.y };
        const int8_t s = m_slotOfTrackId[tid];

        if (s >= 0)
        {
            // 已知 track_id
            seen[s] = true;
            Slot& slot = m_slots[s];
            if (slot.pressed)
            {
                // 移动更新（去重：坐标无变化不重复发）
                if (slot.point.x != pt.x || slot.point.y != pt.y)
                {
                    slot.point = pt;
                    slot.hasNewData = true;
                }
            }
            else
            {
                // 槽处于"release 待 LVGL 消费"状态，同 track_id 手指重新出现
                // → 暂存按压，不覆盖 release
                slot.pendingPress = true;
                slot.pendingPoint = pt;
            }
        }
        else
        {
            // 新 track_id → 找真正空闲槽
            const int f = findFreeSlot();
            if (f >= 0)
            {
                m_slotOfTrackId[tid] = (int8_t)f;
                Slot& slot = m_slots[f];
                slot.trackId = (int8_t)tid;
                slot.point = pt;
                slot.pressed = true;
                slot.hasNewData = true;
                slot.pendingPress = false;
                seen[f] = true;
            }
            // 全忙 → 丢弃（路由状态式，下帧槽释放后自补）
        }
    }

    // 已绑定但本帧未出现 → 进入 release 状态（保持绑定，待 LVGL 消费）
    for (int s = 0; s < kSlotCount; s++)
    {
        Slot& slot = m_slots[s];
        if (slot.trackId != -1 && !seen[s])
        {
            if (slot.pressed)
            {
                slot.pressed = false;
                slot.hasNewData = true;
            }
            // ⚠ 手指消失即取消暂存按压：若 pendingPress 未消费（release 尚未被 LVGL
            //   读取）手指就抬起，indevReadCb 会在消费 RELEASE 时把暂存按压提升为
            //   PRESSED → 以过期坐标交付一次虚假按压 + 下帧补 RE → "幽灵点击"误触按钮。
            slot.pendingPress = false;
            slot.pendingPoint = {};
        }
    }
}

void GT911Indev::getSlots(SlotInfo* out, size_t max) const
{
    Lock lock(m_mutex);
    const size_t n = max < (size_t)kSlotCount ? max : (size_t)kSlotCount;
    for (size_t i = 0; i < n; i++)
    {
        const Slot& s = m_slots[i];
        out[i].active  = (s.trackId != -1);
        out[i].pressed = s.pressed;
        out[i].trackId = s.trackId;
        out[i].point   = s.point;
    }
}

void GT911Indev::indevReadCb(lv_indev_t* drv, lv_indev_data_t* data)
{
    const int s = (int)(intptr_t)lv_indev_get_driver_data(drv);
    auto& self = instance();

    if (s < 0 || s >= kSlotCount)
    {
        data->point.x = 0;
        data->point.y = 0;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    Lock lock(self.m_mutex);
    Slot& slot = self.m_slots[s];

    if (slot.hasNewData)
    {
        // 交付待消费的状态变化
        data->point = slot.point;
        data->state = slot.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        slot.hasNewData = false;

        if (!slot.pressed)
        {
            // RELEASED 已交付 → release 握手
            if (slot.pendingPress)
            {
                // 提升暂存按压：下个读即 PRESSED
                slot.pressed = true;
                slot.point = slot.pendingPoint;
                slot.pendingPress = false;
                slot.hasNewData = true;
            }
            else
            {
                // 真正释放 → 释放绑定
                if (slot.trackId >= 0 && slot.trackId < kTrackIdRange)
                    self.m_slotOfTrackId[slot.trackId] = -1;
                slot.trackId = -1;
            }
        }
        data->continue_reading = slot.hasNewData || slot.pendingPress;
    }
    else
    {
        // 无新数据：维持当前状态
        data->point = slot.point;
        data->state = slot.pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        data->continue_reading = slot.pendingPress;
    }
}
