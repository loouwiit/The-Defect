/*
 * C++ 框架符号表 — 供 .so 插件调用的主固件 C++ 类方法。
 *
 * 两类：
 *   1. App 非内联方法（app.cpp 定义，插件子类继承/调用必需）—— 用 asm 重命名声明
 *      取其 mangled 符号地址（GCC/Clang 扩展：extern fn __asm__("_ZN...")）。
 *   2. 单例直出（用户选定设计）：instance() + 方法直接注册，.so 像原生代码一样
 *      VirtualGamepad::instance().getState(...)。
 *
 * ⚠ mangled 名必须匹配主固件实际编译产物。不再手推：build 后用
 *   `tools/check_elf_syms.sh build/The-Defect.elf` 一键校验（脚本列出本表全部符号）；
 *   ❌ 的符号用 `tools/check_elf_syms.sh <elf> <类名/方法名>` 搜实际 mangled 名修正。
 */
#include "esp_elf.h"
#include "app/app.hpp"
#include "display/display.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "gamepad/bleGamepad.hpp"
#include "battery/batteryManager.hpp"

/* ── App 非内联方法（asm 重命名到真实 mangled 名） ────────── */
extern void App_ctor_c1(void* self, void* display) __asm__("_ZN3AppC1EP7Display");
extern void App_ctor_c2(void* self, void* display) __asm__("_ZN3AppC2EP7Display");
extern void App_dtor_d1(void* self) __asm__("_ZN3AppD1Ev");
extern void App_dtor_d2(void* self) __asm__("_ZN3AppD2Ev");
extern void App_pushApp(void* self, void* app) __asm__("_ZN3App7pushAppEPS_");
extern void App_popApp(void* self) __asm__("_ZN3App6popAppEv");
extern void App_replaceWith(void* self, void* app) __asm__("_ZN3App11replaceWithEPS_");
extern void App_setManager(void* self, void* mgr) __asm__("_ZN3App10setManagerEP15AppStackManager");

/* ── 单例非静态方法（asm 重命名取址，避免成员函数指针 cast） ── */
/* VirtualGamepad::getState(int8_t,int8_t) const */
extern void* VirtualGamepad_getState(void* self, int8_t h, int8_t p) __asm__("_ZNK14VirtualGamepad8getStateEaa");
/* BleGamepad::connectedCount() const */
extern void* BleGamepad_connectedCount(void* self) __asm__("_ZNK10BleGamepad14connectedCountEv");
/* BatteryManager::getPercent() / getVoltageMv() / isCharging() */
extern void* BatteryManager_getPercent(void* self)   __asm__("_ZN14BatteryManager10getPercentEv");
extern void* BatteryManager_getVoltageMv(void* self) __asm__("_ZN14BatteryManager12getVoltageMvEv");
extern void* BatteryManager_isCharging(void* self)   __asm__("_ZN14BatteryManager10isChargingEv");

extern const struct esp_elfsym g_elf_framework_syms[] = {
    /* App 构造/析构（插件子类构造基类子对象需 C2/D2；delete 走 App* 虚析构链） */
    { "_ZN3AppC1EP7Display",            (void*)&App_ctor_c1 },
    { "_ZN3AppC2EP7Display",            (void*)&App_ctor_c2 },
    { "_ZN3AppD1Ev",                    (void*)&App_dtor_d1 },
    { "_ZN3AppD2Ev",                    (void*)&App_dtor_d2 },

    /* App 导航便利方法（插件内调用 pushApp/popApp/replaceWith） */
    { "_ZN3App7pushAppEPS_",          (void*)&App_pushApp },
    { "_ZN3App6popAppEv",             (void*)&App_popApp },
    { "_ZN3App11replaceWithEPS_",     (void*)&App_replaceWith },
    { "_ZN3App10setManagerEP15AppStackManager", (void*)&App_setManager },

    /* 单例直出 — VirtualGamepad（静态 instance + 成员 getState） */
    { "_ZN14VirtualGamepad8instanceEv", (void*)&VirtualGamepad::instance },
    { "_ZNK14VirtualGamepad8getStateEaa",(void*)&VirtualGamepad_getState },

    /* 单例直出 — BleGamepad */
    { "_ZN10BleGamepad8instanceEv",     (void*)&BleGamepad::instance },
    { "_ZNK10BleGamepad14connectedCountEv", (void*)&BleGamepad_connectedCount },

    /* 单例直出 — BatteryManager */
    { "_ZN14BatteryManager10getPercentEv",   (void*)&BatteryManager_getPercent },
    { "_ZN14BatteryManager12getVoltageMvEv", (void*)&BatteryManager_getVoltageMv },
    { "_ZN14BatteryManager10isChargingEv",   (void*)&BatteryManager_isCharging },

    ESP_ELFSYM_END
};
