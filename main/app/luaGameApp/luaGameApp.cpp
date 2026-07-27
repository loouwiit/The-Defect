#include "app/luaGameApp/luaGameApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include <esp_log.h>
#include <cstring>

// ============================================================
//  构造 / 析构
// ============================================================

LuaGameApp::LuaGameApp(Display* display, const char* scriptPath)
    : App(display)
    , m_scriptPath(scriptPath)
{
}

LuaGameApp::LuaGameApp(Display* display, const char* scriptCode, size_t codeLen)
    : App(display)
    , m_scriptCode(scriptCode, codeLen)
    , m_useCode(true)
{
}

LuaGameApp::~LuaGameApp()
{
    // 停止游戏循环
    m_loopRunning = false;
}

// ============================================================
//  registerBindings — 将上下文注入 Lua 绑定模块
// ============================================================

void LuaGameApp::registerBindings()
{
    lua_State* L = m_lua.state();
    if (!L) return;

    // 注册标准模块
    luaRegisterModules(L);

    // 将上下文指针存入 Lua registry，供绑定函数通过 getCtx() 获取
    lua_pushlightuserdata(L, &m_dispCtx);
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_DISPLAY_CTX");

    lua_pushlightuserdata(L, &m_inputCtx);
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_INPUT_CTX");
}

// ============================================================
//  init
// ============================================================

void LuaGameApp::init()
{
    App::init();

    // 1. 创建 LVGL UI
    {
        auto guard = display->lockGuard();
        if (!guard) {
            ESP_LOGE(TAG, "无法锁定显示");
            return;
        }

        // 画布容器（子对象每帧清空，Lua 在此绘制）
        m_canvas = lv_obj_create(screen);
        lv_obj_remove_style_all(m_canvas);
        lv_obj_set_size(m_canvas, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(m_canvas, 0, 0);
        lv_obj_set_style_bg_opa(m_canvas, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(m_canvas, 0, 0);
        lv_obj_set_scrollbar_mode(m_canvas, LV_SCROLLBAR_MODE_OFF);

        // 状态标签（画布上层，不被每帧清空影响）
        m_statusLabel = lv_label_create(screen);
        lv_obj_set_pos(m_statusLabel, LV_HOR_RES - 120, 10);
        lv_obj_set_style_text_font(m_statusLabel,
            FontLoader::getDefault(FontLoader::FontSize::Small), 0);
        lv_label_set_text(m_statusLabel, "Lua init...");
        lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
    }

    // 2. 初始化 Lua VM
    if (!m_lua.init()) {
        ESP_LOGE(TAG, "Lua VM 初始化失败");
        updateStatus();
        return;
    }

    // 3. 设置绑定上下文
    m_dispCtx.canvas = m_canvas;
    m_dispCtx.screen = screen;
    m_dispCtx.display = display;

    for (int i = 0; i < MaxPlayers; i++) {
        m_inputCtx.states[i] = &m_inputs[i];
        m_inputCtx.prevStates[i] = &m_prevInputs[i];
    }

    // 4. 注册绑定
    registerBindings();

    // 5. 加载脚本
    bool loaded = false;
    if (m_useCode) {
        loaded = m_lua.loadString(m_scriptCode);
    }
    else {
        loaded = m_lua.loadScript(m_scriptPath.c_str());
    }

    if (!loaded) {
        ESP_LOGE(TAG, "脚本加载失败");
        updateStatus();
        return;
    }

    m_luaReady = true;

    // 6. 调用 setup()（可选）
    if (m_lua.hasFunction("setup")) {
        m_lua.callFunction("setup", 0, 0);
    }

    // 7. 更新状态显示
    {
        auto guard = display->lockGuard();
        if (guard) {
            lv_label_set_text(m_statusLabel, "Lua OK");
            lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUCCESS, 0);
        }
    }

    ESP_LOGI(TAG, "LuaGameApp init 完成，脚本: %s",
        m_useCode ? "(memory)" : m_scriptPath.c_str());
}

// ============================================================
//  deinit
// ============================================================

void LuaGameApp::deinit()
{
    // 停止游戏循环
    m_loopRunning = false;
    m_luaReady = false;

    // 等待 Task 退出
    while (m_thread.isRunning())
        vTaskDelay(pdMS_TO_TICKS(20));

    App::deinit();
}

// ============================================================
//  onForeground / onBackground
// ============================================================

void LuaGameApp::onForeground()
{
    App::onForeground();
    m_paused = false;

    // 启动游戏循环 Task（如果还没启动）
    if (!m_thread.isRunning() && m_luaReady) {
        m_loopRunning = true;
        m_lastWakeTime = xTaskGetTickCount();
        m_fpsTick = esp_timer_get_time() / 1000;
        m_frameCount = 0;

        m_thread = Thread{ loopTaskFunc, "luaGameLoop",this,Thread::Priority::Normal, 32768 };
        if (!m_thread.isRunning())
        {
            ESP_LOGE(TAG, "游戏循环 Task 创建失败");
            m_loopRunning = false;
        }
    }
}

void LuaGameApp::onBackground()
{
    App::onBackground();
    m_paused = true;
}

// ============================================================
//  onGamepadInput — 缓存输入快照
// ============================================================

void LuaGameApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
    if (hostId != 0 || playerId >= MaxPlayers) return;
    m_inputs[playerId] = state;
}

// ============================================================
//  游戏循环（C++ Task 驱动，60fps）
// ============================================================

void LuaGameApp::loopTaskFunc(void* param)
{
    auto* self = static_cast<LuaGameApp*>(param);

    while (self->m_loopRunning) {
        self->gameLoop();

        // 固定 60fps
        vTaskDelayUntil(&self->m_lastWakeTime, pdMS_TO_TICKS(16));
    }

    // Task 退出：弹出当前 App
    Task::addTask([](void* p) -> TickType_t {
        static_cast<LuaGameApp*>(p)->popApp();
        return Task::infinityTime;
        }, "luaGameExit", self, 0, Task::Affinity::None);
    self->m_thread = {};
}

void LuaGameApp::gameLoop()
{
    if (!m_luaReady || m_paused) return;

    // FPS 统计
    m_frameCount++;
    uint32_t now = esp_timer_get_time() / 1000;
    if (now - m_fpsTick >= 1000) {
        int fps = m_frameCount * 1000 / (int)(now - m_fpsTick);
        m_frameCount = 0;
        m_fpsTick = now;
        // 更新状态标签
        auto guard = display->lockGuard();
        if (guard && m_statusLabel) {
            char buf[32];
            snprintf(buf, sizeof(buf), "FPS: %d", fps);
            lv_label_set_text(m_statusLabel, buf);
        }
    }

    // 计算 dt（距上帧毫秒数）
    static uint32_t s_lastFrameTime = now;
    float dt = (float)(now - s_lastFrameTime);
    s_lastFrameTime = now;
    if (dt < 0.1f) dt = 16.0f;     // 初始值
    if (dt > 100.0f) dt = 16.0f;   // 防止跳帧

    // 更新输入上下文（备份上一帧）
    memcpy(m_prevInputs, m_inputs, sizeof(m_inputs));
    m_inputCtx.playerCount = countConnectedPlayers();

    // 持 LVGL 锁 → 清空画布 → 调 Lua loop(dt) → 释放锁
    {
        auto guard = display->lockGuard();

        // 清空画布
        lv_obj_clean(m_canvas);

        // 调用 Lua loop(dt)
        lua_State* L = m_lua.state();
        if (L) {
            lua_pushnumber(L, dt);
        }
        if (!m_lua.callFunction("loop", 1, 1)) {
            // 出错时标记为不继续
            m_luaReady = false;
            if (m_statusLabel) {
                std::string err = m_lua.popString();
                if (err.empty()) err = "Lua Error";
                lv_label_set_text(m_statusLabel, err.c_str());
                lv_obj_set_style_text_color(m_statusLabel, GUI::Color::DANGER, 0);
            }
            return;
        }

        // 检查返回值（栈顶可能有错误信息，先检查）
        bool shouldContinue = true;
        if (lua_gettop(m_lua.state()) > 0) {
            shouldContinue = m_lua.popBool();
        }
        if (!shouldContinue) {
            m_loopRunning = false;
        }
    }
}

// ============================================================
//  统计已连接玩家数
// ============================================================

int LuaGameApp::countConnectedPlayers() const
{
    int count = 0;
    for (int i = 0; i < MaxPlayers; i++) {
        if (m_inputs[i].buttons != 0 || m_inputs[i].dpad != 15) {
            count++;
        }
    }
    return count;
}

// ============================================================
//  restart
// ============================================================

void LuaGameApp::restart()
{
    // 停止当前循环
    m_loopRunning = false;
    while (m_thread.isRunning())
        vTaskDelay(pdMS_TO_TICKS(20));

    // 重新初始化 Lua
    // 析构旧的 LuaRuntime，隐式调用 lua_close
    // 但因为 m_lua 是成员变量且没有提供 reset 方法，需要重新 init
    // 简单做法：deinit 当前 App 然后重新 init
    // 更好的做法：使用替换机制
    replaceWith(new LuaGameApp(display,
        m_useCode ? m_scriptCode.c_str() : m_scriptPath.c_str(),
        m_useCode ? m_scriptCode.size() : 0));
}

// ============================================================
//  更新状态显示
// ============================================================

void LuaGameApp::updateStatus()
{
    auto guard = display->lockGuard();
    if (!guard || !m_statusLabel) return;

    lv_label_set_text(m_statusLabel, "Lua Error");
    lv_obj_set_style_text_color(m_statusLabel, GUI::Color::DANGER, 0);
}
