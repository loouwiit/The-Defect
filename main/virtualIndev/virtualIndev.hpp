#pragma once

#include "display/display.hpp"
#include "mutex/mutex.hpp"
#include "lvgl.h"

class VirtualIndev
{
public:
    static VirtualIndev& instance();

    bool start(Display* display);

    // Inject a touch event: state: LV_INDEV_STATE_PR or LV_INDEV_STATE_REL
    void sendTouch(lv_indev_state_t state, lv_point_t point);

private:
    VirtualIndev() = default;
    ~VirtualIndev() = default;

    lv_indev_t* indev = nullptr;
    Display* display = nullptr;

    Mutex mutex;

    struct Sample {
        lv_indev_state_t state = LV_INDEV_STATE_REL;
        lv_point_t point{0, 0};
        bool updated = false;
    } sample;

    static VirtualIndev* s_instance;

    static void indevReadCb(lv_indev_t* drv, lv_indev_data_t* data);
};
