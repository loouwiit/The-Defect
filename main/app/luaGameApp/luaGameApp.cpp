#include "app/luaGameApp/luaGameApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "storage/fat.hpp"
#include "app/aiAgentApp/aiAgentOverlay.hpp"
#include "gamepad/bleGamepad.hpp"
#include "touch/gt911Indev.hpp"
#include <esp_log.h>
#include <cstring>
#include <cstdio>

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
//  createFromAIGenerated — AI 生成代码的静态工厂
// ============================================================

LuaGameApp* LuaGameApp::createFromAIGenerated(Display* display,
                                               const std::string& name,
                                               const std::string& code)
{
    // 确保目标目录存在
    newFloor("/root/lua");
    newFloor("/root/lua/game");

    // 将 AI 生成的代码写入固定路径
    const char* path = "/root/lua/game/test.lua";
    FILE* f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "无法写入 %s", path);
        return nullptr;
    }

    size_t written = fwrite(code.c_str(), 1, code.size(), f);
    fclose(f);

    if (written != code.size()) {
        ESP_LOGE(TAG, "写入不完整: %zu/%zu", written, code.size());
        return nullptr;
    }

    ESP_LOGI(TAG, "AI 生成游戏已保存: %s (\"%s\", %zu bytes)",
             path, name.c_str(), code.size());

    return new LuaGameApp(display, path);
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

    lua_pushlightuserdata(L, &m_touchCtx);
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_TOUCH_CTX");
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
        // 禁用滚动 + 点击消费：lv_obj_create 默认可滚动，子对象越出画布边界时触摸拖拽会滚动整个画布；
        // 移除 CLICKABLE 让 LVGL 单指针不消费触摸（与 FruitNinja 画布同款），避免与 Lua 轮询冲突
        lv_obj_remove_flag(m_canvas, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

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
    {
        ESP_LOGI(TAG, "等待Lua线程退出...");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

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

    // FreeRTOS 任务不允许 return，必须自杀
    // 注意：popApp 由 requestExit() 调度，此处不重复调用
    ESP_LOGI(TAG, "lua循环退出");
    self->m_thread = {};
}

// ============================================================
//  防死循环看门狗
// ============================================================
// 单次 loop(dt) 执行的 Lua 指令上限。正常游戏帧远低于此值；
// 恶意脚本（如 while true do end）会持续消耗指令计数，触发 hook 中断。
static constexpr int kMaxInstructionsPerFrame = 10000000;  // 1000 万条指令/帧

static void luaWatchdogHook(lua_State* L, lua_Debug* ar)
{
    (void)ar;
    // 在保护调用（lua_pcall）内触发 longjmp，安全中断脚本执行
    luaL_error(L, "script timeout (infinite loop?)");
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

    // 同步真实连接状态（BLE 链接），供 input.connected() / player_count() 使用
    for (int i = 0; i < MaxPlayers; i++) {
        const auto* dev = BleGamepad::instance().getDevice(i);
        m_inputCtx.connected[i] = dev && dev->connected;
    }
    m_inputCtx.playerCount = countConnectedPlayers();

    // 触摸快照：在 lockGuard 之前（无锁阶段）轮询 GT911 槽位，避免持 LVGL 锁期间等 GT911 mutex
    updateTouchSnapshot();

    // 持 LVGL 锁 → 清空画布 → 调 Lua loop(dt) → 释放锁
    {
        auto guard = display->lockGuard();

        // 清空画布
        lv_obj_clean(m_canvas);

        // 调用 Lua loop(dt)
        lua_State* L = m_lua.state();
        if (L) {
            lua_pushnumber(L, dt);
            // 安装防死循环看门狗（指令计数 hook），调用返回后移除
            lua_sethook(L, luaWatchdogHook, LUA_MASKCOUNT, kMaxInstructionsPerFrame);
        }
        if (!m_lua.callFunction("loop", 1, 1)) {
            // 出错时：上报错误给 AI + 退出
            if (m_statusLabel) {
                // 使用 getLastError() 而非 popString()，因为 checkError 已保存错误
                std::string err = m_lua.getLastError();
                if (err.empty()) err = "Lua Error";
                auto guard2 = display->lockGuard();
                if (guard2) {
                    lv_label_set_text(m_statusLabel, err.c_str());
                    lv_obj_set_style_text_color(m_statusLabel, GUI::Color::DANGER, 0);
                }
                // 报告给 AI Agent，下次对话注入 system prompt
                AiAgentOverlay::reportLuaError(err);
            }
            if (L) lua_sethook(L, nullptr, 0, 0);  // 移除看门狗
            requestExit();
            return;
        }

        // 移除看门狗 hook
        if (L) lua_sethook(L, nullptr, 0, 0);

        // 检查返回值
        bool shouldContinue = true;
        if (lua_gettop(m_lua.state()) > 0) {
            shouldContinue = m_lua.popBool();
        }
        if (!shouldContinue) {
            requestExit();  // 统一退出门
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
        if (m_inputCtx.connected[i]) count++;
    }
    return count;
}

// ============================================================
//  触摸快照 — 每帧在 loop(dt) 前（无锁阶段）轮询 GT911Indev 槽位
//  推进顺序：先 down→prevDown 归档，再 getSlots() 刷新当前（顺序不可反）
// ============================================================

void LuaGameApp::updateTouchSnapshot()
{
    // 先归档上一帧，再刷新当前帧
    memcpy(m_touchCtx.prevDown, m_touchCtx.down, sizeof(m_touchCtx.prevDown));

    GT911Indev::SlotInfo slots[GT911Indev::kSlotCount];
    GT911Indev::instance().getSlots(slots, GT911Indev::kSlotCount);

    int downCount = 0;
    for (int i = 0; i < GT911Indev::kSlotCount; i++)
    {
        const bool down = slots[i].active && slots[i].pressed;
        m_touchCtx.down[i] = down;
        m_touchCtx.x[i]    = (int16_t)slots[i].point.x;
        m_touchCtx.y[i]    = (int16_t)slots[i].point.y;
        if (down) downCount++;
    }
    m_touchCtx.downCount = downCount;
}

// ============================================================
//  requestExit — 统一退出请求
// ============================================================

void LuaGameApp::requestExit()
{
    if (!m_loopRunning) return;  // 防重入
    m_loopRunning = false;

    // 由 Task 调度器延后执行 popApp，避免在 LVGL 锁内或线程上下文中直接操作栈
    Task::addTask([](void* p) -> TickType_t {
        auto* app = static_cast<LuaGameApp*>(p);
        if (app->getManager()) {
            app->popApp();
        }
        return Task::infinityTime;
        }, "luaGameExit", this, 0, Task::Affinity::None);
}

// ============================================================
//  restart
// ============================================================

void LuaGameApp::restart()
{
    // 通过 requestExit 统一退出（设标志 + 调度 popApp）
    requestExit();
    // 等待线程退出
    while (m_thread.isRunning())
        vTaskDelay(pdMS_TO_TICKS(20));

    // replaceWith 创建新实例替换当前栈顶
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
// ============================================================
//  getAiContext — AI 上下文：告知当前运行脚本路径
// ============================================================

std::string LuaGameApp::getAiContext()
{
    std::string ctx = "当前在 Lua 游戏运行器。\n";

    if (m_scriptPath.empty()) {
        ctx += "当前运行脚本: (内存，无文件路径)\n";
    } else {
        ctx += "当前运行脚本: " + m_scriptPath + "\n";
        ctx += "可使用 read 操作读取脚本源码。\n";
        ctx += "示例: {\"actions\":[{\"action\":\"read\",\"text\":\"" + m_scriptPath + "\"}],\"nextRound\":true}\n";
    }

    return ctx;
}