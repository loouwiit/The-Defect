# AI Game Plan — The-Defect 集成 AI 游戏能力方案

> 基于 [ESP-Claw](https://github.com/espressif/esp-claw) 的 "聊天造物" 模式，为 The-Defect 多人游戏机添加 AI 动态生成游戏能力。

---

## 项目对比

| 维度 | ESP-Claw (参考) | The-Defect (本项目) |
|------|----------------|-------------------|
| 芯片 | ESP32-S3 | **ESP32-P4** (主控) + C6 (手柄) |
| 屏幕 | ST7789 240×320 SPI | **720p MIPI DSI** (大屏) |
| 语言 | C 语言 | **C++** |
| UI | 自有绘图 API | **LVGL** |
| 游戏方式 | **AI 实时生成** Lua 脚本 | 预编译 C++ 游戏 |
| 多人 | 无原生支持 | **ESP-NOW / BLE 多机联机** |
| AI Agent | ✅ 原生内置 | ❌ 需要集成 |

The-Defect 的优势在于 **P4 强劲性能 + 720p 大屏 + 多人联机架构**，缺的是 AI Agent 动态能力。

---

## 阶段一：嵌入 Lua 脚本引擎

Lua 是 AI 生成游戏脚本的运行时基础。AI 生成的 Lua 代码通过绑定层调用硬件能力。

### 1.1 添加 Lua 组件

ESP-IDF 本身有 Lua 的组件封装。在 `main/idf_component.yml` 或 `CMakeLists.txt` 中引入 Lua：

```cmake
# main/CMakeLists.txt
idf_component_register(
    SRCS main.cpp ...
    INCLUDE_DIRS .
    REQUIRES lua ...  # 添加 lua 组件
)
```

### 1.2 创建 Lua 绑定层

在 `main/lua_bridge/` 下创建绑定模块，将 C++ 引擎能力暴露给 Lua。

#### 显示绑定 (`lua_bridge_display.cpp`)

将 LVGL 绘图功能暴露为 Lua API：

```cpp
static int l_draw_rect(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    int color = luaL_checkinteger(L, 5);
    // 调用 LVGL lv_draw_rect
    return 0;
}

static int l_draw_text(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    int size = luaL_checkinteger(L, 4);
    // 调用 LVGL lv_label
    return 0;
}

static const luaL_Reg display_lib[] = {
    {"draw_rect", l_draw_rect},
    {"draw_text", l_draw_text},
    {"fill_screen", l_fill_screen},
    {"draw_image", l_draw_image},
    {NULL, NULL}
};

int luaopen_display(lua_State *L) {
    luaL_newlib(L, display_lib);
    return 1;
}
```

#### 输入绑定 (`lua_bridge_input.cpp`)

读取手柄/按键状态：

```cpp
static int l_read_button(lua_State *L) {
    const char *btn = luaL_checkstring(L, 1);
    int pressed = inputManager->isPressed(btn);
    lua_pushboolean(L, pressed);
    return 1;
}
```

#### 音频绑定 (`lua_bridge_audio.cpp`)

播放音效：

```cpp
static int l_play_tone(lua_State *L) {
    int freq = luaL_checkinteger(L, 1);
    int duration = luaL_checkinteger(L, 2);
    audioPlayer->playTone(freq, duration);
    return 0;
}
```

#### 线程/延时绑定 (`lua_bridge_thread.cpp`)

```cpp
static int l_sleep(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}
```

### 1.3 Lua 模块清单

| Lua 模块 | 绑定源 | 功能 |
|---------|--------|------|
| `display` | C++ LVGL API | 画矩形、圆、文字、图片，清屏 |
| `input` | C++ 输入管理层 | 读取按键/触摸/摇杆状态 |
| `audio` | C++ 音频驱动 | 播放音效/音乐 |
| `thread` | FreeRTOS | 延时、后台任务 |
| `math` | 标准 Lua | 随机数、计算 |

### 1.4 Lua 游戏脚本示例 (AI 生成的贪吃蛇)

```lua
-- AI 生成的 snake.lua
local display = require("display")
local input = require("input")
local math = require("math")

local W, H = 720, 1280
local cell = 20
local snake = {{x=5, y=10}}
local food = {x=math.random(1, 35), y=math.random(1, 63)}
local dir = {x=1, y=0}
local score = 0

while true do
    -- 读按键
    if input.read("up") and dir.y == 0 then dir = {x=0, y=-1}
    elseif input.read("down") and dir.y == 0 then dir = {x=0, y=1}
    elseif input.read("left") and dir.x == 0 then dir = {x=-1, y=0}
    elseif input.read("right") and dir.x == 0 then dir = {x=1, y=0}
    end

    -- 移动蛇
    local head = {x=snake[1].x + dir.x, y=snake[1].y + dir.y}
    if head.x == food.x and head.y == food.y then
        score = score + 1
        food = {x=math.random(1, 35), y=math.random(1, 63)}
    else
        table.remove(snake)
    end
    table.insert(snake, 1, head)

    -- 碰撞检测
    for i = 2, #snake do
        if snake[i].x == head.x and snake[i].y == head.y then
            display.draw_text(200, 600, "GAME OVER", 48)
            return
        end
    end

    -- 绘制
    display.fill_screen("#0c1220")
    display.draw_text(10, 10, "Score: " .. score, 24)
    for _, s in ipairs(snake) do
        display.draw_rect(s.x * cell, s.y * cell, cell-2, cell-2, "#00ff00")
    end
    display.draw_rect(food.x * cell, food.y * cell, cell, cell, "#ff0000")
    display.refresh()

    thread.sleep(150) -- 帧率控制
end
```

---

## 阶段二：添加 AI Agent 核心

在 AppStack 中新增一个 **AI Agent App**，负责聊天交互和游戏生成。

### 2.1 App 位置

```
main/app/
├── apps/
│   ├── tetrisApp/
│   ├── snakeApp/
│   ├── chessApp/
│   └── aiAgentApp/         ← 新增
│       ├── aiAgentApp.hpp
│       └── aiAgentApp.cpp
```

### 2.2 工作流

```
用户输入: "写一个贪吃蛇"
    ↓
aiAgentApp 显示加载界面
    ↓
HTTP 请求 LLM API (OpenAI/DeepSeek/Qwen)
    ↓
System Prompt 告诉 LLM:
  - 屏幕分辨率 720×1280
  - 可用 Lua API (display/input/audio/thread)
  - 按键映射和编号
  - 必须输出完整可运行的 Lua 脚本
    ↓
LLM 返回 Lua 游戏代码
    ↓
保存到文件 → 加载到 Lua 虚拟机 → 运行
    ↓
用户在 LVGL 界面看到游戏运行
```

### 2.3 核心代码框架

```cpp
// aiAgentApp.cpp
void aiAgentApp::onUserMessage(const char* message) {
    // 1. 显示 "AI 生成中..."
    showLoadingScreen();

    // 2. 调用 LLM API
    std::string luaCode = callLLM(systemPrompt, message);

    // 3. 保存到文件
    saveToFile("/root/skills/current_game.lua", luaCode);

    // 4. 执行 Lua 游戏
    stopCurrentGame();            // 停止之前的游戏
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    luaopen_display(L);           // 注册显示绑定
    luaopen_input(L);             // 注册输入绑定
    luaopen_audio(L);             // 注册音频绑定
    luaL_dofile(L, "/root/skills/current_game.lua");
    // 游戏在独立的 FreeRTOS task 中运行
}
```

### 2.4 多人扩展

AI Agent 的 System Prompt 中加入多人 API，即可让 AI 生成联机游戏：

```cpp
// Lua 多人同步 API
static int l_net_sync(lua_State *L) {
    const char *gameState = luaL_checkstring(L, 1);
    // 通过 ESP-NOW 广播游戏状态到其他 P4
    espNowBroadcast(gameState);
    return 0;
}

static int l_net_recv(lua_State *L) {
    // 接收其他玩家输入
    const char *input = espNowReceive();
    lua_pushstring(L, input);
    return 1;
}
```

这样 AI 就能生成多人对战游戏，如对战俄罗斯方块、贪吃蛇大作战等。

---

## 阶段三：事件路由器 (可选)

轻量级事件路由，实现"按 B 键直接启动上次 AI 游戏"的毫秒级响应。

### 3.1 实现位置

```
main/eventRouter/
├── eventRouter.hpp
└── eventRouter.cpp
```

### 3.2 核心设计

```cpp
// eventRouter.hpp
enum class ActionKind {
    RUN_LUA_SCRIPT,
    SWITCH_APP,
    SEND_MESSAGE,
};

struct EventRule {
    const char* source;     // "button", "timer", "joystick"
    const char* event;      // "single_click", "long_press"
    const char* key;        // "btn_b", "timer_game_tick"
    ActionKind action;
    const char* actionArg;  // "/root/skills/current_game.lua"
};

class EventRouter {
public:
    void addRule(const EventRule& rule);
    void removeRule(const char* id);
    void dispatch(const Event& event);
private:
    std::vector<EventRule> rules;
};

// eventRouter.cpp
void EventRouter::dispatch(const Event& event) {
    for (auto& rule : rules) {
        if (match(rule, event)) {
            execute(rule.action, rule.actionArg);
            if (rule.consumeOnMatch) break;
        }
    }
}
```

### 3.3 路由规则示例

```json
{
  "id": "btn_b_play_ai_game",
  "source": "button",
  "event": "single_click",
  "key": "btn_b",
  "action": "run_lua",
  "actionArg": "/root/skills/current_game.lua"
}
```

用户配置后，按 **B 键** 即可直接启动 AI 最近生成的游戏，无需经过 LLM。

---

## 阶段四：利用 The-Defect 独特优势

这是 **ESP-Claw 做不到** 的差异化能力：

### 4.1 AI 生成多人联机游戏

```
AI 生成游戏 → 保存为 .lua → 主机执行
    ↓
通过 ESP-NOW 同步游戏状态到其他 P4 主机
    ↓
多台 P4 主机各自渲染，共享同一局游戏状态
```

### 4.2 AI 生成多手柄游戏

```
P4 主机 (server):
  ├── 运行 AI 生成的 Lua 游戏
  ├── 接收 C6 手柄输入 (UART)
  ├── 接收其他 P4 输入 (ESP-NOW)
  └── 统一渲染所有玩家状态到屏幕 (或分屏)
```

### 4.3 AI 生成游戏 + 屏幕串流

AI 生成的游戏可以通过你的 ScreenStream 模块实时投屏到浏览器。

---

## 推荐实施路线

| 步骤 | 内容 | 工作量 | 效果 |
|------|------|--------|------|
| 1️⃣ | **嵌入 Lua** + 暴露 LVGL 绘图绑定 | ~1-2 天 | AI 可以画图形到屏幕 |
| 2️⃣ | **添加按键/输入 Lua 绑定** | ~0.5 天 | AI 可以响应手柄操作 |
| 3️⃣ | **实现 LLM HTTP 客户端** + AI Agent App | ~1 天 | AI 可以生成并运行游戏 |
| 4️⃣ | **整合到 AppStack** | ~0.5 天 | AI 游戏作为独立 App 运行 |
| 5️⃣ | **多人联网绑定** | ~1-2 天 | AI 可以生成联机对战游戏 |
| 6️⃣ | **事件路由器** | ~0.5 天 | 按键直启游戏，毫秒级响应 |

**最快 3 天** 可以达到"对着游戏机说话就能生成游戏"的效果。

---

## 参考项目

- [ESP-Claw](https://github.com/espressif/esp-claw) — 乐鑫 IoT AI Agent 框架，本项目核心参考
- [ESP-Claw 文档](https://esp-claw.com/zh-cn/) — Board Manager、Lua 模块等参考
- [The-Defect readme](./readme.md) — 本项目原始架构文档
