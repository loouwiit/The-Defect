#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * @brief Lua touch 绑定的上下文
 *
 * 通过 lua_upvalue 注入到 touch API 函数中。
 * LuaGameApp 在每帧游戏循环前（loop(dt) 之前、无锁阶段）更新：
 *   ① down[] → prevDown[]（把上一帧"当前"归档为"上一帧"）
 *   ② GT911Indev::getSlots() → 刷新 down[]/x[]/y[]/downCount
 * 边沿检测在绑定内完成（is_just_pressed = down && !prevDown）。
 */
struct LuaTouchContext {
    /// 槽位数（与 GT911Indev::kSlotCount 一致）
    static constexpr int kSlotCount = 5;

    bool down[kSlotCount]{};       // 当前帧各槽位是否按下
    bool prevDown[kSlotCount]{};   // 上一帧各槽位按下状态（边沿检测用）
    int16_t x[kSlotCount]{};       // 当前坐标（屏幕坐标，与 display 模块一致）
    int16_t y[kSlotCount]{};
    int downCount{0};              // 当前按下的槽位数
};

/// luaopen 函数，供 luaRegisterModules 注册时使用
#ifdef __cplusplus
extern "C" {
#endif
int luaopen_touch(lua_State* L);
#ifdef __cplusplus
}
#endif
