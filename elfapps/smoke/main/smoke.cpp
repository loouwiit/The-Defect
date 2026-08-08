/*
 * smoke 插件 — Phase 1 探测
 * 验证：T0 加载链路（dlopen/version/dlsym）+ T3 最小渲染 + P7 单例直出 + 静态对象规则
 * 符号面：tools/gen_elf_syms.py 白名单生成（elf_symtab.c），.so include 主固件头文件原生调用。
 * 构建：cd elfapps/smoke && idf.py -G 'Unix Makefiles' so
 * 上传：curl -X PUT --data-binary @build/smoke.so http://<host>/root/elf/smoke.so
 */
#include "elf_sdk.h"
#include "app/app.hpp"
#include "task/task.hpp"
#include "battery/batteryManager.hpp"
#include "gamepad/bleGamepad.hpp"
#include <cstdio>
#include <vector>
#include <string>

/* R1 探测 P2：文件级非 POD static —— 预期不被构造（loader 无 .init_array）。
 * 哨兵结构：构造=0xDEAD，未构造=0（bss 清零）。只读，安全。 */
struct CtorProbe {
    int val = 0;
    CtorProbe() { val = 0xDEAD; }
};
static CtorProbe g_fileStaticProbe;

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
    __attribute__((visibility("default")));

class SmokeApp : public App
{
public:
    explicit SmokeApp(Display* display) : App(display) {}
    ~SmokeApp() override = default;

    void init() override
    {
        App::init();

        // P2a：函数局部 static（预期构造 — __cxa_guard 运行时执行）
        static CtorProbe fnLocalProbe;
        m_fnLocalOk = (fnLocalProbe.val == 0xDEAD);

        // P1：C++ 运行时（std::vector/string 分配）
        m_stdlibOk = probeStdlib();

        // P7：单例直出（Task::addTask/removeTask 静态 + BatteryManager/BleGamepad 单例）
        probeFramework();

        // P2b：文件级非 POD static 探测
        m_fileStaticVal = g_fileStaticProbe.val;

        if (auto guard = display->lockGuard())
        {
            lv_obj_set_style_bg_color(screen, lv_color_hex(0x1a1a2e), 0);
            lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

            m_status = lv_label_create(screen);
            lv_obj_center(m_status);
            lv_obj_set_style_text_color(m_status, lv_color_hex(0xffffff), 0);

            updateStatus();
        }

        printf("[ELF-TEST] init: stdlib=%s fnLocal=%s fileStatic=0x%X\n",
               m_stdlibOk ? "PASS" : "FAIL",
               m_fnLocalOk ? "PASS" : "FAIL",
               m_fileStaticVal);
    }

    void deinit() override
    {
        printf("[ELF-TEST] deinit\n");
        App::deinit();
    }

    void onForeground() override
    {
        printf("[ELF-TEST] onForeground\n");
        App::onForeground();
    }

    void onBackground() override
    {
        printf("[ELF-TEST] onBackground\n");
        App::onBackground();
    }

    void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override
    {
        // P5 输入虚分派：A=刷新状态 B=退出
        if (state.isPressed(GamepadButton::BTN_A))
            updateStatus();
        if (state.isPressed(GamepadButton::BTN_B))
            popApp();   // 退出 → 回 TestApp
    }

private:
    // P1：C++ 运行时探测（std::vector/string）
    bool probeStdlib()
    {
        std::vector<int> v;
        for (int i = 0; i < 100; i++) v.push_back(i);
        std::string s = "elf-smoke-";
        char num[16];
        snprintf(num, sizeof(num), "%u", (unsigned)v.size());
        s += num;
        return v.size() == 100 && s == "elf-smoke-100";
    }

    // P7 单例直出：插件 include 主固件头文件，直接调单例/静态方法（原生风格）。
    // 符号由 tools/gen_elf_syms.py 白名单生成（Task/BatteryManager/BleGamepad 类）。
    void probeFramework()
    {
        // Task 静态方法（替代 elf_task_add/remove；探针立即清理，无泄漏）
        m_taskOk = false;
        if (auto* t = Task::addTask([](void*) -> TickType_t { return Task::infinityTime; },
                                    "elf-probe", nullptr, 0, Task::Affinity::None))
        {
            m_taskOk = true;
            Task::removeTask(t);
        }
        // BatteryManager / BleGamepad 单例直出
        m_battPct = BatteryManager::instance().getPercent();
        m_padCount = BleGamepad::instance().connectedCount();
        m_padBatt = BleGamepad::instance().getBatteryLevel(0);
    }

    void updateStatus()
    {
        if (!m_status) return;
        if (auto guard = display->lockGuard())
        {
            char buf[180];
            snprintf(buf, sizeof(buf),
                     "SMOKE v%d\n"
                     "stdlib=%s fnLocal=%s\n"
                     "fileStatic=0x%X (%s)\n"
                     "task=%s batt=%d%% pads=%u padBatt=%u\n"
                     "A:刷新 B:退出",
                     (int)ELF_SDK_VERSION,
                     m_stdlibOk ? "PASS" : "FAIL",
                     m_fnLocalOk ? "PASS" : "FAIL",
                     m_fileStaticVal,
                     m_fileStaticVal == 0xDEAD ? "CTOR?" : "no-ctor(预期)",
                     m_taskOk ? "PASS" : "FAIL",
                     m_battPct,
                     (unsigned)m_padCount,
                     (unsigned)m_padBatt);
            lv_label_set_text(m_status, buf);
        }
    }

    lv_obj_t* m_status{};
    bool m_stdlibOk = false;
    bool m_fnLocalOk = false;
    int m_fileStaticVal = 0;
    bool m_taskOk = false;
    int m_battPct = -1;
    unsigned m_padCount = 0;
    unsigned m_padBatt = 255;
};

/* ── 唯一 C 入口（default 可见性） ── */

static void* smoke_create_app(void* display, const void* params)
{
    (void)params;
    return new SmokeApp((Display*)display);
}

static void smoke_destroy_app(void* app)
{
    delete (App*)app;
}

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
{
    static const elf_plugin_info_t info = {
        ELF_SDK_VERSION,
        "smoke",
        smoke_create_app,
        smoke_destroy_app,
    };
    return &info;
}
