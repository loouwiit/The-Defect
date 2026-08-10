#pragma once

#include "app/app.hpp"
#include "lua/luaRuntime.hpp"
#include "lua/luaModules.hpp"
#include "lua/bindings/luaDisplay.hpp"
#include "lua/bindings/luaInput.hpp"
#include "lua/bindings/luaTouch.hpp"
#include "task/task.hpp"

/**
 * @brief Lua 游戏运行 App
 *
 * 在独立 Lua VM 中加载并运行 Lua 游戏脚本。
 * C++ 驱动的游戏循环以 60fps 固定帧率调用 Lua loop(dt) 函数。
 *
 * 契约函数：
 *   function setup()          — 初始化（可选）
 *   function loop(dt) → bool  — 每帧调用，返回 false 退出（必选）
 *   function cleanup()        — 清理（可选）
 */
class LuaGameApp : public App {
public:
    static constexpr char TAG[] = "LuaGameApp";

    /// 从 FAT 文件加载脚本
    LuaGameApp(Display* display, const char* scriptPath);

    /// 从内存代码字符串加载脚本
    LuaGameApp(Display* display, const char* scriptCode, size_t codeLen);

    ~LuaGameApp() override;

    void init() override;
    void deinit() override;

    void onForeground() override;
    void onBackground() override;

    void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

    /// @brief AI 上下文：告知当前运行的脚本路径
    std::string getAiContext() override;

    /// 请求退出游戏（设标志 + 调度 popApp，线程安全）
    void requestExit();

    /// 重启当前脚本
    void restart();

    /// 从 AI 生成的代码创建并启动游戏（保存到 /root/lua/game/test.lua）
    /// @param display 显示驱动
    /// @param name    游戏名称（来自 action.text，仅用于日志）
    /// @param code    Lua 脚本源码
    static LuaGameApp* createFromAIGenerated(Display* display,
                                              const std::string& name,
                                              const std::string& code);

private:
    // ── Lua VM ──
    LuaRuntime m_lua;
    bool m_luaReady{false};

    // ── 脚本源 ──
    std::string m_scriptPath;
    std::string m_scriptCode;
    bool m_useCode{false};

    // ── LVGL 对象 ──
    lv_obj_t* m_canvas{};        // 容器（LVGL 对象池父级，全屏）
    lv_obj_t* m_statusLabel{};   // FPS/错误状态显示

    // ── 游戏循环 Task ──
    Thread m_thread{};
    bool m_loopRunning{false};
    bool m_paused{false};
    TickType_t m_lastWakeTime{};
    int m_frameCount{};
    uint32_t m_fpsTick{};
    uint32_t m_lastFrameTime{0};   // 上帧时间戳（成员而非 static：static 跨实例共享，restart 后 dt 污染）

    // ── 输入快照（hostId=0 本地玩家） ──
    GamepadState m_inputs[MaxPlayers]{};
    GamepadState m_prevInputs[MaxPlayers]{};

    // ── 绑定上下文 ──
    LuaDisplayContext m_dispCtx{};
    LuaInputContext m_inputCtx{};
    LuaTouchContext m_touchCtx{};

    // ── 去重 ──
    TickType_t m_nextActionTime{};
    static constexpr TickType_t ACTION_COOLDOWN = 500;

    // ── 内部方法 ──
    void registerBindings();
    void gameLoop();
    void updateTouchSnapshot();
    int countConnectedPlayers() const;
    void updateStatus();

    static void loopTaskFunc(void* param);
};
