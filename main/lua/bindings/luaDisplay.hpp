#pragma once

#include "lvgl.h"
#include "display/display.hpp"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * @brief Lua display 绑定的上下文
 *
 * 通过 Lua registry 注入到每个 display API 函数中。
 * 对象池化：Lua 脚本的绘制调用按帧内绘制序号复用 LVGL 对象（静态跳过），
 * 避免每帧创建/销毁对象，LVGL 原生局部重绘。
 */
struct LuaDisplayContext {
    lv_obj_t* canvas{};       // 容器（LVGL 对象池父级，全屏）
    lv_obj_t* screen{};       // 全屏 screen
    Display* display{};

    uint16_t bg565{};         // 最近 fill_screen 颜色（RGB565）
    bool bgSet{};             // 是否已设置背景（fill_screen 静态跳过用）

    // ── 绘制槽位对象池（按帧内绘制序号复用；参数与上帧相同则跳过更新） ──
    static constexpr int MaxSlots = 64;
    struct Slot {
        lv_obj_t* obj = nullptr;
        uint8_t kind = 0;     // 0=none, 1=shape(lv_obj), 2=text(lv_label)
        int16_t x = 0, y = 0, w = 0, h = 0;
        uint16_t color = 0;
        char text[48] = {};
    };
    Slot slots[MaxSlots];
    int slotIndex = 0;        // 本帧已用槽位
    int prevSlotCount = 0;    // 上一帧槽位数
};

/// luaopen 函数，供 luaRegisterModules 注册时使用
#ifdef __cplusplus
extern "C" {
#endif
int luaopen_display(lua_State* L);
#ifdef __cplusplus
}
#endif

/// 帧前：清理上一帧多出的槽位 + 重置槽位索引（须持锁下调用）
void luaDisplayBeginFrame(LuaDisplayContext* ctx);
