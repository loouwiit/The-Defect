# The-Defect — Agent Guidelines

## Project Overview

ESP32-P4 驱动的多人游戏主机 + ESP32-C6 无线手柄。屏幕为 6寸 720×1280 MIPI ILI9881C，触摸 GT911 (I²C)，GUI 使用 LVGL 9.4。

## Build & Flash

使用 `@espIdfCommands` 工具（ESP-IDF 扩展）进行编译、烧录、监控等操作，不要直接执行 bash 命令。

## Partition Table

```
# Name,   Type, SubType, Offset, Size,    Flags
nvs,      data, nvs  ,         , 16k
otadata,  data, ota  ,         ,  8k,
phy_init, data, phy  ,         ,  4k,
ota_0,    app , ota_0,         ,  6M
fat,      data, fat  ,      16M, 16M,
```

分区说明：
| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 16KB | NVS 键值持久化（WiFi 配置、手柄配对、音量、mDNS 主机名、时间自动同步开关等） |
| otadata | 8KB | OTA 启动选择数据 |
| phy_init | 4KB | WiFi/BT 物理层校准数据 |
| ota_0 | 6MB | 应用固件（单 OTA slot） |
| fat | 16MB | FAT 数据分区，挂载到 `/root/` |

## Resources

`reserces/` 目录下的文件会被上传到 FAT 分区，挂载路径为 `/root/`。

```
reserces/
├── lua/              ← Lua 测试/示例脚本
├── server/          ← HTTP 服务器静态资源（端口 80）
│   ├── index.html
│   ├── index.css
│   ├── screen/      ← 屏幕串流 Web 查看器
│   ├── tetris/      ← Tetris 网页客户端
│   ├── gamepad/     ← 虚拟手柄 Web 客户端（/ws/input 测试）
│   └── wifi/        ← WiFi 网页配置
└── system/          ← 系统文件（字体 + 输入法词库）
    ├── NotoSC.ttf   ← 全局默认字体
    ├── pinyin_dict.txt      ← 拼音词库（411 码 / 8343 字，Rime 8105 规范汉字表）
    ├── wubi_dict.txt        ← 五笔词库（21812 码 / 26414 字，Rime 五笔86极点码表）
    └── wubi_pinyin_dict.txt ← 五笔+拼音混合词库（22088 码 / 25042 字，五笔优先拼音兜底）
```

---

## Architecture — 完整模块概览

| 层次 | 模块 | 路径 | 职责 |
|------|------|------|------|
| **基础设施** | Task | `main/task/` | 协程调度器（轮询）+ Thread 封装，Priority/Affinity 管理 |
| | Mutex | `main/mutex/` | FreeRTOS `xSemaphoreCreateMutex` RAII 封装 (`Mutex` + `Lock`) |
| | GPIO | `main/gpio/` | GPIO 装饰器封装，`=` / `bool` / `int` 运算符重载，ISR 注册 |
| | IIC | `main/iic/` | I²C 主控总线 (`IIC`) + 设备封装 (`IICDevice`)，自动端口选择 |
| | VirtualGamepad | `main/gamepad/virtualGamepad.hpp` | 统一手柄输入抽象层，60fps Dispatch + hostMask 掩码 + 远程转发 |
| | TimeService | `main/time/timeService.hpp` | 全局 SNTP 时间同步服务：自动同步 NVS 持久化 + WiFi 连接自动同步 + 周期校正 + 句柄化多回调 |
| **显示/输入** | Display | `main/display/display.hpp` | ILI9881C 初始化 + LVGL 适配层，`LockGuard` RAII（递归锁） |
| | ILI9881c | `main/display/ili9881c.cpp` | 面板驱动（MIPI DSI），背光亮度控制 |
| | FontLoader | `main/display/font.cpp` | FreeType 字体加载，3 字号全局默认字体，VFS 驱动器 'F' |
| | Touch | `main/touch/` | GT911 驱动 (I²C)，`Touch::update()` 读全 5 点含 track_id |
| | GT911Indev | `main/touch/gt911Indev.hpp` | **五 indev 多指桥接层**：INT 中断驱动 → track_id→槽位路由 → 5 个独立 lv_indev（POINTER），release 握手防 track_id 复用跳变 |
| | VirtualIndev | `main/virtualIndev/` | 虚拟触摸输入设备（Web 触摸注入），无锁去重 + hasNewData/continue_reading |
| **导航** | App | `main/app/app.hpp` | 应用基类（生命周期、栈钩子、手柄输入） |
| | AppStack | `main/app/appStack.hpp` | 单栈导航（push/pop/replace + 异步删除） |
| | AppStackManager | `main/app/appStackManager.hpp` | 多栈管理器（根栈 + 游戏独立栈 + 前台切换） |
| **AI Agent** | AiClient / AiParser / AiExecutor / AiSystemInfo | `main/aiAgent/` | OpenAI API 调用、JSON 解析、动作执行、系统信息采集 |
| | AiAgentOverlay | `main/app/aiAgentApp/` | 系统层浮动窗（`lv_layer_sys`），对话历史 + 长期记忆 |
| | PinyinDict | `main/app/aiAgentApp/pinyinDict.hpp` | 输入法词库加载（拼音/五笔/混合），FAT 文本 → `lv_ime_pinyin_set_dict` |
| | App (基类) | `main/app/app.hpp` | `getAiContext()` / `onAiSelect()` / `onAiActivate()` + 可选 `executeAiAction()` |
| **Lua 引擎** | LuaRuntime | `main/lua/luaRuntime.hpp` | Lua 5.5 VM RAII 封装，loadScript/loadString/callFunction |
| | luaDisplay | `main/lua/bindings/luaDisplay.cpp` | 简化帧缓冲绘图 API（fill_rect/draw_text/draw_circle 等） |
| | luaInput | `main/lua/bindings/luaInput.cpp` | 手柄输入轮询 API（is_pressed/joystick/connected） |
| | luaAudio | `main/lua/bindings/luaAudio.cpp` | 音频播放 API（play_sfx/play_bgm/set_volume） |
| | luaThread | `main/lua/bindings/luaThread.cpp` | 线程/延时 API（sleep/now/random） |
| | luaStorage | `main/lua/bindings/luaStorage.cpp` | 安全存档 API（save/load/remove/exists，限定 `/root/lua/data/`） |
| | LuaGameApp | `main/app/luaGameApp/` | Lua 游戏运行 App，C++ 驱动 60fps 循环调用 Lua loop(dt) |
| | LuaManagerApp | `main/app/luaManagerApp/` | Lua 脚本管理器（浏览/运行/删除/重命名，AI 生成入口） |
| **市场** | MarketClient / MarketConfig | `main/market/` | 游戏市场 HTTP 客户端（异步 GET 列表/脚本/图片 + POST 统计） |
| **应用** | DesktopApp | `main/app/desktopApp/` | 桌面启动器，**7** 游戏卡片（含 Lua 脚本管理 + 游戏市场），3 焦点组导航 |
| | TetrisApp | `main/app/tetris/` | 俄罗斯方块（Host-authoritative，3P，WebSocket 网络对战） |
| | FruitNinjaApp | `main/app/fruitNinja/` | 水果忍者（含房间系统，多玩家；canvas 化渲染器） |
| | Snake | `main/app/snake/` | 贪吃蛇（snakeGame + snakeRoom） |
| | ChineseChess | `main/app/chineseChess/` | 中国象棋（含 AI 引擎） |
| | AiAgentOverlay | `main/app/aiAgentApp/` | AI 助手浮动窗（`lv_layer_sys`，可拖拽） |
| | MarketApp | `main/app/marketApp/` | 游戏市场：截图卡片网格 + 详情 + 下载运行/本地运行 |
| BleSettingsApp | `main/app/bleSettingsApp/` | BLE 手柄配对/管理 UI |
| | WifiSettingsApp | `main/app/wifiSettingsApp/` | WiFi 扫描/连接 UI |
| | TimeSettingsApp | `main/app/timeSettingsApp/` | 时间/SNTP 设置 UI |
| | PowerManagementApp | `main/app/powerManagementApp/` | 电源管理（电量、关机 Deep-sleep） |
| | TestApp | `main/app/testApp/` | 测试用 App |
| | ConsoleManagerApp | `main/app/consoleManager/` | 多主机管理（mDNS 发现其他主机） |
| **跨主机联机** | HostManager | `main/host/hostManager.hpp` | 角色管理（主机/从机）、4 槽位管理、WsClient 连接池、mask 下发 |
| | wsConsoleHandler | `main/host/wsConsoleHandler.cpp` | `/ws/console` 管理通道（注册/全量 slots_update 广播/心跳） |
| | wsInputHandler | `main/host/wsInputHandler.cpp` | `/ws/input` WebSocket 入口（Web 客户端用）→ VirtualGamepad |
| | udpInput | `main/host/udpInput.cpp` | UDP 端口 8082 监听 + 发送（P4→P4 低延迟输入转发） |
| | WsClient | `main/websocket/wsClient.hpp` | 通用 WebSocket 客户端封装，`sendText` / `sendBinary` |
| | ConsoleManagerApp | `main/app/consoleManager/` | 多主机管理 UI（BLE 配对风格，4 槽位 + mask 控制 + 交换） |
| | **tetrisTcpHost** | `main/app/tetris/net/tetrisTcpHost.hpp` | Tetris TCP 游戏服务器（端口 8083），异步 send Task 20fps，主机→从机 snapshot |
| | **TetrisRemoteClient** | `main/app/tetris/net/tetrisRemoteClient.hpp` | Tetris 远程客户端，listen 端口 8084 等待主机连接，recv Task 解析 snapshot |
| | **TetrisRemoteApp** | `main/app/tetris/tetrisRemoteApp.hpp` | Tetris 远程游戏 App，全屏渲染远程主机画面，多玩家动态分屏 |
| **网络** | WiFi | `main/wifi/wifi.hpp` | C 风格 API：STA/AP 双模 + NAT，`esp_wifi_remote` C6 通信 |
| | C6 Slave | `main/wifi/slave.hpp` | SDIO/ESP-HOSTED 协处理器管理器，后台 Task 早期启动 |
| | SocketStream | `main/wifi/socketStream.hpp` | TCP 输入/输出/双向流封装（`ISocketStream` / `OSocketStream` / `IOSocketStream`） |
| | SocketStreamWindow | `main/wifi/socketStreamWindow.hpp` | 连接池管理器（HTTP 6 worker 线程） |
| | HTTP Server | `main/server/` | 裸 socket TCP HTTP，端口 80，6 工作者线程，内置 HTML |
| | WS Server | `main/wsServer/` | WebSocket 端口 8080，动态 URI 注册/注销，广播 |
| | mDNS | `main/wifi/mdns.hpp` | mDNS 服务发现（`_http` 80, `_ws` 8080, `_p4console` 80） |
| | Discovery | `main/discovery/` | mDNS 设备发现（查询 `_p4console._tcp` 枚举其他主机） |
| | HTTP | `main/wifi/http.hpp` | HTTP 协议解析（Method/Status/Header 等） |
| **媒体** | ScreenStream | `main/screenStream/` | MJPEG 硬件 JPEG 编码 + WebSocket 流，直接读 bridge 帧缓冲 |
| | ES8311 | `main/audio/ES8311.hpp` | ES8311 编解码器驱动（I²C + I²S） |
| | Audio | `main/audio/Audio.hpp` | 音频管理器（Meyer's Singleton）+ AudioHandle 值类型 |
| **外设** | BLE Gamepad | `main/bleGamepad/` | NimBLE + esp_hosted，4 玩家上限，NVS 持久化配对 |
| Lua Runtime | `main/lua/` | Lua 5.5 脚本引擎 + 4 个绑定模块（display/input/audio/thread） |
| LuaGameApp | `main/app/luaGameApp/` | Lua 游戏运行 App，AI 生成的 .lua 脚本在此运行 |
| | 手柄固件 | [`gamepad/`](https://codeberg.org/loouwiit/esp32c6HidJoystick) (submodule) | ESP32-C6 BLE HID 手柄固件，独立仓库 |
| | Battery Manager | `main/battery/` | ADC 电池电量估算 + 充电检测 (GPIO23) + 充电动画 |
| | CPU Monitor | `main/monitor/` | FreeRTOS 任务利用率差分采样，串口输出 |
| **GUI** | GUI | `main/gui/` | 色板（8 色）+ 14 个工厂方法 (`createPage/Card/Flex/Button/Title/Switch/Slider` 等) |
| **存储** | FAT | `main/storage/fat.hpp` | FAT 分区 (16MB)，VFS `/root/`，文件/目录 CRUD |
| | memFS | `main/storage/mem.hpp` | 内存文件系统 (6MB max)，VFS `/root/mem` |
| | SD | `main/storage/sd.hpp` | SD 卡，VFS `/root/sd` |
| **补丁** | Patches | `patches/` | 6 个 patch：帧回调 / PPA 队列 / SRM bypass / WS transport / LVGL IME iuv 过滤 / count uint16 |

---

## Lua 引擎模块

**模块路径**: `main/lua/`

### LuaRuntime — Lua VM RAII 封装

```cpp
class LuaRuntime {
    LuaRuntime();
    ~LuaRuntime();                          // lua_close()
    bool init();                            // 打开安全标准库（沙箱）
    bool loadScript(const char* path);      // luaL_loadfile + lua_pcall
    bool loadString(const char* code);      // luaL_loadstring + lua_pcall
    bool callFunction(const char* name, int nargs=0, int nresults=0);
    bool hasFunction(const char* name);
    const std::string& getLastError() const; // 获取上次错误
    lua_State* state();
};
```

### Lua 绑定模块

所有绑定在 `main/lua/bindings/` 下，提供简化高层 API：

| 模块 | 文件 | Lua API |
|------|------|---------|
| luaDisplay | `bindings/luaDisplay.cpp` | `fill_screen/fill_rect/draw_rect/draw_text/draw_line/draw_circle/draw_image` |
| luaInput | `bindings/luaInput.cpp` | `is_pressed/is_just_pressed/joystick/buttons/connected/player_count` |
| luaAudio | `bindings/luaAudio.cpp` | `play/play_sfx/play_bgm/stop_bgm/set_volume` |
| luaThread | `bindings/luaThread.cpp` | `sleep/now/random` |
| luaStorage | `bindings/luaStorage.cpp` | `save/load/remove/exists`（安全存档，限定 `/root/lua/data/`） |

颜色格式：支持 `"#ff0000"` / `0xff0000` / `{r=255,g=0,b=0}` 三种。

### Lua 沙箱（安全边界）

`LuaRuntime::init()` 只打开**安全标准库**（base/coroutine/math/string/table/utf8），并移除 base 里的 `dofile`/`loadfile`；**io / os / package / debug 一律不开放**。所有 Lua 脚本（AI 生成 + 市场下载）统一沙箱化。

- 文件读写只能通过 `storage` 模块（白名单 `/root/lua/data/`，禁止路径穿越，单文件 ≤256KB）
- 防死循环：`LuaGameApp` 每帧调用 `loop(dt)` 前挂指令计数 hook（`lua_sethook`，1000 万条/帧上限），超限中断并走 `reportLuaError()` 错误路径
- 效果：脚本无法读取系统文件（如 AI API Key）、无法删改系统数据

### LuaGameApp — Lua 游戏运行器

**模块路径**: `main/app/luaGameApp/`

```cpp
class LuaGameApp : public App {
    LuaGameApp(Display* display, const char* scriptPath);
    LuaGameApp(Display* display, const char* scriptCode, size_t codeLen);
    static LuaGameApp* createFromAIGenerated(Display* display,
                                              const std::string& name,
                                              const std::string& code);
};
```

**游戏循环契约**（Lua 脚本必须实现）：
```lua
function setup() end                     -- 初始化（可选）
function loop(dt) return true end        -- 每帧调用，返回 false 退出
```

C++ 驱动：独立 Task @60fps → 持 LVGL 锁 → 更新输入 → 调 Lua `loop(dt)` → 释放锁 → `vTaskDelayUntil`

### AI 集成动作

| 动作 | 说明 | 示例 |
|------|------|------|
| `generate_lua_game` | AI 生成 Lua 游戏并运行 | `{"action":"generate_lua_game","text":"贪吃蛇","params":{"code":"..."}}` |
| `read` | AI 读取 FAT 文件内容（配 `nextRound:true`） | `{"action":"read","text":"/root/lua/game/test.lua","nextRound":true}` |

**错误回传**：Lua 运行时错误自动存入 `AiAgentOverlay::m_pendingLuaError`，下次对话注入 system prompt，AI 可修复后重新生成。`read` 读取的文件内容通过 `m_pendingFileRead` 同样注入 system prompt。两者均在 `onSend()` 和 `nextRound` follow-up 双路径中注入。

---

## 基础设施层

### Task — 协程调度器

**模块路径**: `main/task/`

简单的轮询式协程调度器，每个 `Task` 包含一个函数指针和定时参数，在 daemon 线程中循环调用。

```cpp
class Task {
public:
    // ── 优先级常量 ──
    class Priority {
        static constexpr UBaseType_t Deamon  = 2;
        static constexpr UBaseType_t Normal  = 5;
        static constexpr UBaseType_t High    = 6;
        static constexpr UBaseType_t RealTime = 8;
    };

    // ── CPU 亲和性 ──
    class Affinity {
        static constexpr size_t None = -1;        // 无限制
        static constexpr size_t NotAssigned = -2; // 未指定
    };

    // 函数返回下次唤醒延迟（ms），infinityTime = portMAX_DELAY
    using Function_t = TickType_t(*)(void* param);

    static void init(size_t daemonThreadCount);
    static Task* addTask(Function_t func, const char* name,
                         void* param = nullptr,
                         TickType_t callTick = 0,
                         Affinity affinity = NotAssigned);
    static void removeTask(Task* task);
    static void setAffinity(Task* task, Affinity affinity);
};
```

**使用方式**:
```cpp
Task::init(2);  // 2 个 daemon 线程

Task::addTask([](void* param)->TickType_t {
    auto& display = *static_cast<Display*>(param);
    ESP_LOGI(TAG, "FPS: %u", display.getFps());
    return 1000;
}, "fpsMonitor", &display, 1000, Task::Affinity::None);
```

### Mutex — RAII 互斥锁

**模块路径**: `main/mutex/`

```cpp
class Mutex {
    Mutex();               // 构造 xSemaphoreCreateMutex
    ~Mutex();              // 确保析构前无人持有
    bool try_lock() const;
    void lock() const;
    void unlock() const;
};

class Lock {
    Lock(const Mutex& mutex);   // 构造时 lock
    ~Lock();                    // 析构时 unlock
};
```

**与 Display::LockGuard 的区别**:
- `Mutex` / `Lock` — 用于 BleGamepad 等模块内部的多线程保护，**非递归**互斥锁
- `Display::LockGuard` — LVGL 内部**递归互斥锁** (`xSemaphoreCreateRecursiveMutex`)，同一 task 可嵌套加锁

### GPIO — 引脚封装

**模块路径**: `main/gpio/`

```cpp
class GPIO {
    constexpr static GPIO_NUM NC = GPIO_NUM_NC;

    GPIO(GPIO_NUM gpio);
    GPIO(GPIO_NUM gpio, Mode mode);
    GPIO(GPIO_NUM gpio, Mode mode, Pull pull, Interrupt intr,
         function_t func = nullptr, void* param = nullptr);

    GPIO& operator=(bool level);  // 写电平
    operator bool();              // 读电平
    operator GPIO_NUM();          // 取引脚号
    operator int();

    void setMode(Mode mode);
    void setInterrupt(Interrupt intr, function_t func, void* param);
    static void enableGlobalInterrupt();
};
```

### IIC — I²C 总线

**模块路径**: `main/iic/`

```cpp
class IIC {
    IIC(GPIO clock, GPIO data, IicPort port = IicPortAuto);
    ~IIC();
    bool detect(uint16_t address);          // 检测设备是否存在
    i2c_master_bus_handle_t getBusHandle();
};

class IICDevice {
    IICDevice(IIC& iic, uint16_t address, unsigned speed = 100000);
    bool transmit(const void* buffer, size_t size);
    bool receive(void* buffer, size_t size);
    bool request(const void* write, size_t writeSize, void* read, size_t readSize);
};
```

ES8311 编解码器、GT911 触摸均通过 IIC 通信。

### VirtualGamepad — 统一手柄输入抽象层

**模块路径**: `main/gamepad/virtualGamepad.hpp`

系统层单例，汇集所有手柄输入源（本地 BLE + 远程客户端），
在专用 Dispatch Task 中以固定 60fps 全量分发到活跃 App。

```cpp
class VirtualGamepad {
public:
    static VirtualGamepad& instance();
    void start(Display* display);
    /// 统一注入（hostId: 0=本机, 1-3=远程设备。按掩码过滤后 dispatchTask 分发）
    void inject(int8_t hostId, int8_t playerId, const GamepadState& state);
    void resetPlayer(int8_t hostId, int8_t playerId);
    void setHostMask(int8_t hostId, uint8_t playerMask);
    void setAllMasks(const uint8_t masks[4]);

    // ── 远程转发（从机模式，UDP）──
    void startForwarder();    // 启动独立 Thread 通过 UDP 转发本机输入
    void stopForwarder();
};
```

**架构**:
```
BLE Task  → inject(0, p, state)     ← 本机
UDP Task  → inject(hostId, p, state) ← 远程（来自 UDP 端口 8082）
WS Task   → inject(hostId, p, state) ← 远程（来自 /ws/input，Web 客户端）
                                              ↓
VirtualGamepad:
  ├── m_states[hostId][playerId] = state  (存储)
  ├── forwarderThread @60fps UDP → 远程主机端口 8082（从机模式）
  └── dispatchTask() @60fps → app->onGamepadInput(hostId, playerId, state)
```

| 特性 | 说明 |
|------|------|
| inject(hostId, ...) | hostId=0 本机，1-3 远程；仅存状态，不修改 mask |
| 分发频率 | 固定 60fps (16ms)，独立 Dispatch Task |
| 全量更新 | 每次遍历 `m_hostMask` 启用的 (hostId, playerId) |
| 掩码默认 | `{0x0F, 0x00, 0x00, 0x00}`（仅本机 P0-P3 启用） |
| 掩码来源 | 主机由 ConsoleManager UI 控制；从机接收主机下发的反码 |
| AI Overlay | `isVisible()` 时跳过分发，AI 独占输入 |
| 从机转发 | forwarderThread 后检查 `~m_hostMask[0] & 0x0F` 决定转发哪些玩家到主机 |
| 传输方式 | UDP（11 字节/帧），持久 socket `connect()` + `send()`，零创建开销 |
| 注入去重 | 状态未变化时不转发（forwarder 侧有 `m_lastForwarded` 去重） |

**`onGamepadInput` 签名**:
```cpp
// hostId: 0=本机, 1-3=从机；playerId: 设备上的手柄编号 (0-3)
virtual void onGamepadInput(int8_t hostId, int8_t playerId, const GamepadState& state);
```

**掩码语义**（由 ConsoleManager 控制，wsConsoleHandler 广播到从机）：
- **主机侧**: `m_hostMask[hostId]` 的 bit[playerId] = 1 → 该输入进入本地游戏
- **从机侧**: 收到主机 mask 后存为**反码**，`m_hostMask[0]` bit=1 → 留在本地处理；bit=0 → forwarder 取反后转发到主机
- 同一手柄输入不会同时被本地和远程消费（mask 互斥）

**集成点**:
- BleGamepad::processTask → `inject(0, playerId, state)` 代替直接调 App
- BleGamepad::start 末尾 → `VirtualGamepad::instance().start(display)`
- BLE_GAP_EVENT_DISCONNECT → `resetPlayer(0, playerId)`
- ConsoleManager::applyMaskToVirtualGamepad → 触发 `wsConsoleBroadcastSlotsUpdate`

### `/ws/input` — Web 客户端远程手柄输入通道

**模块路径**: `main/host/wsInputHandler.cpp`

接收 11 字节二进制帧（来自 Web 客户端 gamepad.html），解析后注入 VirtualGamepad。

**协议格式**:
| 偏移 | 大小 | 字段 |
|------|------|------|
| 0 | 1 | `hostId` — 设备号 (0=本机保留, 1-3=远程) |
| 1 | 1 | `localPlayer` — 设备上的手柄编号 (0-3) |
| 2-3 | 2 | `buttons` — uint16 小端序 |
| 4 | 1 | `lx` |
| 5 | 1 | `ly` |
| 6 | 1 | `rx` |
| 7 | 1 | `ry` |
| 8 | 1 | `lt` |
| 9 | 1 | `rt` |
| 10 | 1 | `dpad` |

- fd → hostId 追踪，断开时 `resetPlayer(hostId, *)` 清理
- P4→P4 联机使用 UDP（端口 8082），不经过此通道

### UDP Input — P4→P4 低延迟输入转发

**模块路径**: `main/host/udpInput.cpp`

两台 P4 设备间的手柄输入转发。发送方在 `forwarderThread` 中通过持久 UDP socket 发送 11 字节帧，
接收方在后台 Task 中监听 UDP 端口 8082。

```cpp
// 发送方（从机 forwarderThread 自动调用）
int sock = udpInputCreateSocket(host, 8082);   // 有 connect()
udpInputSend(sock, buf, 11);                   // 用 send()，无连接创建开销
udpInputCloseSocket(sock);

// 接收方（udpInputStart() 在 main.cpp 中启动）
// 后台 Task 监听端口 8082，recvfrom → VirtualGamepad::inject()
```

**协议格式**：与 `/ws/input` 相同（11 字节二进制帧）

| 特性 | 说明 |
|------|------|
| 传输方式 | UDP，每个 datagram 原子到达，无 Nagle 缓冲 |
| 丢包容忍 | 输入帧 60fps，丢一帧不影响后续 |
| Socket 管理 | 持久 socket，`connect()` + `send()`，零 TIME_WAIT |
| 接收方式 | 独立 Task，`recvfrom` 5 秒超时，自动清理断连 source |
| 端口 | 8082（不同于 WS 的 8080）|

### HostManager — 跨主机角色与连接管理

**模块路径**: `main/host/hostManager.hpp`

管理当前设备的角色（主机/从机）、4 设备槽位、WsClient 连接池、mask 下发。

**角色模型**：

| 角色 | 默认 | 触发 |
|------|------|------|
| 主机 (Host) | ✅ | 从未连接他人 |
| 从机 (Client) | ❌ | 用户从 ConsoleManager 点击连接远程主机，**放弃自身主机资格** |

**槽位**（Slot 0=本机，Slot 1-3=从机）：

```
┌───┬───┬───┬───┐
│ 0 │ 1 │ 2 │ 3 │
│本机│ B │ --│ --│
└───┴───┴───┴───┘
```

**从机连接流程**：
```
从机→主机:  WS connect /ws/console
从机→主机:  {"type":"register","hostname":"esp32p4-b","gamepad_count":2}
主机→从机:  {"type":"registered","slot":1}
主机→所有:  slots_update（全量槽位 + mask 广播）
```

**API**：
```cpp
class HostManager {
    static HostManager& instance();

    // ── 角色 ──
    bool isHost() const;
    bool isClient() const;
    int  getMySlot() const;

    // ── 从机侧 ──
    bool connectToHost(const char* ip, uint16_t port = 8080, const char* hostname = nullptr);
    void disconnectFromHost();
    bool isConnectedToHost() const;
    const char* getRemoteHostname() const;
    WsClient* getWsConnection(const char* uri);   // 连接池，同 URI 复用

    // ── 主机侧 ──
    struct ClientSlot {
        int slotId; char hostname[64]; char ip[16];
        int gamepadCount; bool connected;
    };
    const ClientSlot* getSlots() const;

    // ── 主机 mask ──
    void setHostMask(const uint8_t mask[4]);
    const uint8_t* getHostMask() const;

    // ── 事件回调 ──
    void setOnSlotUpdate(std::function<void(const ClientSlot* slots)> cb);
    void setOnRoleChange(std::function<void(bool isClient)> cb);

    // ── 内部（由 wsConsoleHandler 调用）──
    void onClientRegistered(int slotId, const char* hostname, const char* ip, int gamepadCount);
    void onClientDisconnected(int slotId);
    void swapSlots(int slotA, int slotB);  // 同时交换 mask
};
```

### `/ws/console` — 设备管理通道

**模块路径**: `main/host/wsConsoleHandler.cpp`

从机注册到主机的管理通道，独立于游戏数据通道。**所有状态变更使用全量 `slots_update` 广播**（不增量）。

| 消息 | 方向 | 说明 |
|------|------|------|
| `{"type":"register","hostname":"...","gamepad_count":N}` | 从机→主机 | 注册带手柄数 |
| `{"type":"registered","slot":N}` | 主机→从机 | 分配槽位 |
| `{"type":"slots_update","slots":[...]}` | 主机→所有 | **全量**槽位+mask 广播（替代增量消息） |
| `{"type":"ping"}/{"type":"pong"}` | 双向 | 心跳 |

**`slots_update` 格式**（每槽带 `mask` 字段）：
```json
{"type":"slots_update","slots":[
  {"slot":0,"hostname":"esp32p4-a","connected":true,"gamepad_count":0,"mask":15},
  {"slot":1,"hostname":"esp32p4-b","connected":true,"gamepad_count":2,"mask":3},
  {"slot":2,"hostname":"","connected":false,"gamepad_count":0,"mask":0},
  {"slot":3,"hostname":"","connected":false,"gamepad_count":0,"mask":0}
]}
```

触发时机：register / disconnect / swap / mask 变化 → 全量广播一次。

### InputHost — 远程设备连接跟踪

**模块路径**: `main/host/inputHost.hpp`

3 槽位 fd→hostId 映射表，供 `/ws/console` 管理通道使用（后续设计）。
当前由 `/ws/input` handler 用于断开清理。

```cpp
class InputHost {
    static InputHost& instance();
    int  registerClient(int fd, const char* hostname);  // 返回 hostId 1-3, -1 满
    void unregisterClient(int fd);
    int  getHostIdByFd(int fd) const;
};
```

### WsClient — 通用 WebSocket 客户端

**模块路径**: `main/websocket/wsClient.hpp`

薄封装 `esp_websocket_client`，供从机连接远程主机。

```cpp
class WsClient {
    bool connect(const char* host, uint16_t port, const char* uri);
    void disconnect();
    bool isConnected();
    bool sendText(const char* data, size_t len);
    bool sendBinary(const uint8_t* data, size_t len);
    void setOnMessage(std::function<void(const char*, size_t)> cb);
    void setOnConnected(std::function<void()> cb);
    void setOnDisconnected(std::function<void()> cb);
};
```

---

### TimeService — 全局时间同步服务

**模块路径**: `main/time/timeService.hpp` / `timeService.cpp`

系统级单例（`TimeService::instance()`），在 `main.cpp` 的 `wifiStart()` 后 `start()` 一次性初始化 SNTP 并保持运行。

```cpp
class TimeService {
    static TimeService& instance();

    void start();                                   // 初始化 SNTP（仅一次）+ 读 NVS 自动开关 + 注册 GOT_IP 事件
    bool isAutoSync() const;                        // 自动同步开关（NVS 持久化，默认开启）
    void setAutoSync(bool enable);                  // 设置并持久化；开启且已连 WiFi 时立即同步
    void forceSync();                               // 立即重启 SNTP 重新同步（幂等）
    bool isSynced() const;                          // 系统时间是否已同步（tm_year > 70）
    time_t getLastSyncUnix() const;                 // 最近一次成功同步的 Unix 时间

    using SyncCbHandle = uint32_t;
    SyncCbHandle addSyncCb(std::function<void()> cb); // 注册同步完成回调，返回句柄
    void removeSyncCb(SyncCbHandle handle);           // 按句柄定点清除（幂等）
};
```

**行为**：
- **自动同步默认开启 + NVS 持久化**：namespace `"time"` / key `"auto_sync"`，重启保持
- **WiFi 连接时同步**：监听 `IP_EVENT_STA_GOT_IP`，自动模式下调 `forceSync()`
- **周期性同步**：lwIP SNTP 内置轮询（`CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`，1h），服务持续运行即自动校正
- **同步完成通知**：`config.sync_cb`（⚠ ESP-IDF v5.5.2 **无** `NETIF_SNTP_TIME_SYNC` 事件，须用回调）

**多回调 + 定点清除（防悬垂）**：
- `addSyncCb` 支持多个监听者，返回句柄；`removeSyncCb(handle)` 精确移除
- 回调容器 `Mutex` 保护；`notifySyncDone()` **持锁直接调用**（不拷贝），与 `removeSyncCb`（App `deinit()` 调用）互斥
  → 回调执行时注册者必然存活，App 退出后事件线程不再访问已销毁对象
- 约定：回调体内不得再调用 TimeService 方法（防非递归锁死锁），应保持轻量

**⚠ 注意**：`esp_netif_sntp_init()` 只能成功调用一次（重复调用返回 `ESP_ERR_INVALID_STATE`）。
`forceSync()` 用 `esp_netif_sntp_start()`（已启动时自动 stop+init 重启），切勿重复 init。

**接入 App**（`TimeSettingsApp`）：`init()` 从 `isAutoSync()` 同步开关显示 + `addSyncCb` 注册；`deinit()` `removeSyncCb` 清除。

---

## 显示与输入

### Display — LVGL 适配层

**模块路径**: `main/display/display.hpp`

| 方法 | 说明 |
|------|------|
| `init()` | 初始化 LVGL 适配器 |
| `bindDisplay(panel, io, w, h, tearAvoid, rotation)` | 绑定 ILI9881c 面板 |
| `start()` | 启动 LVGL worker task |
| `lockGuard()` | RAII 递归锁获取（LVGL 线程安全） |
| `setBrightness(percent)` | 设置背光（委托到 ILI9881c） |
| `getBrightness()` | 读取当前背光 |
| `saveBrightness()` | 持久化背光到 NVS |
| `getLvglDisplay()` | 获取 `lv_display_t*` |
| `getActiveApp()` | 获取当前前台 App |
| `setStackManager(mgr)` | 绑定 AppStackManager |
| `getFps()` | 获取当前帧率 |

**LockGuard**:
```cpp
if (auto guard = display->lockGuard()) {
    lv_label_create(screen);
    // LVGL 操作...
}   // 析构自动 unlock
```

### ILI9881c — 面板驱动

**模块路径**: `main/display/ili9881c.cpp`

单例模式（`getInstance()`），封装 MIPI DSI 初始化序列（见 `initCommand.inl`）和背光 PWM 控制。

### FontLoader — 字体加载

**模块路径**: `main/display/font.cpp`

```cpp
class FontLoader {
public:
    enum class FontSize : uint16_t {
        Small  = 24,
        Medium = 32,    // Default
        Large  = 56,
    };

    static const lv_font_t* load(const char* vfsPath, uint16_t size);
    static const lv_font_t* getDefault(FontSize size = FontSize::Default);
    static bool setDefault(const lv_font_t* font, FontSize size = FontSize::Default);

private:
    struct FontEntry {
        FontSize size{};
        const lv_font_t* font{};
    };
    static constexpr size_t MaxCount = 3;
    EXT_RAM_BSS_ATTR static FontEntry s_fonts[MaxCount];
};
```

**VFS 驱动器 'F'**: 将 `"F:..."` 映射到 `/root/`，使 FreeType 通过 LVGL VFS 读取 FAT 分区文件。

**字体回退链**: NotoSC 缺少的符号（电池图标等）由 LVGL 内置 symbol 字体提供：
```cpp
const_cast<lv_font_t*>(FontLoader::getDefault(FontSize::Small))->fallback = lv_font_get_default();
```

### Touch — GT911

**模块路径**: `main/touch/`

| 特性 | 值 |
|------|-----|
| 接口 | I²C |
| 地址 | `0x5D` / `0x14` (备用) |
| 分辨率 | 720 × 1280 |
| 最大触控点 | 5 |
| INT/RST | GPIO46 / GPIO47 |

### GT911Indev — 五 indev 多指桥接层

**模块路径**: `main/touch/gt911Indev.hpp` / `gt911Indev.cpp`

单例（`GT911Indev::instance()`），把 GT911 硬件 5 点（含 track_id）转发到 **5 个独立 LVGL pointer indev**。LVGL 每 indev 是独立指针状态机 → 多指可同时按下、各自向光标下对象发 PRESSED/PRESSING/RELEASED，**所有 App 零改动**获得多指 LVGL 事件（`lv_indev_active()` 取各自坐标）。

```
GT911 INT(GPIO47) → ISR 给信号量 → 读线程 (Touch::update 读全 5 点)
   → track_id→槽位路由 → 5×Slot → indevReadCb(slot) → lv_indev#0..4 → LVGL 事件
```

| 特性 | 说明 |
|------|------|
| 中断驱动 | `esp_lcd_touch_register_interrupt_callback_with_data` + 二进制信号量；读线程 `take(sem, 30ms)`（INT 主触发 + 30ms 超时兜底保移动刷新），空闲零轮询 |
| track_id 路由 | `m_slotOfTrackId[256]`（track_id→槽，-1=未分配）+ `Slot[5]`，`Mutex` 保护读线程 ↔ read_cb |
| release 握手 | 槽 RELEASED 被 read_cb 消费前**不得**呈现新按压/坐标（`pendingPress` 暂存）→ 防 track_id 复用"未释放跳坐标"（PRESS_LOST/假点击）；消费后提升 PRESSED 或释放绑定 |
| 接线 | `main.cpp` 用 `GT911Indev::instance().start(&display, &touch)` 取代 `display.bindTouch()`（`display.start()` 之前） |
| 调试 | `getSlots()` 供 TestApp 多指可视化（5 圆点 + 槽号:trackId + 按下/释放态） |

```cpp
class GT911Indev {
    static GT911Indev& instance();
    bool start(Display* display, Touch* touch);
    void stop();
    struct SlotInfo { bool active; bool pressed; int8_t trackId; lv_point_t point; };
    void getSlots(SlotInfo* out, size_t max) const;
};
```

**已知取舍**：多指同时落在可滚动列表会各自触发滚动（LVGL 无"主指"概念）——本机 UI 以按钮/桌面为主，可接受。

### VirtualIndev — 虚拟触摸输入

**模块路径**: `main/virtualIndev/`

单例类，用于从 WebSocket/HTTP 注入触摸事件到 LVGL 输入设备系统。

```cpp
class VirtualIndev {
    static VirtualIndev& instance();
    bool start(Display* display);
    void sendTouch(lv_indev_state_t state, lv_point_t point);
};
```

- **无锁一写一读**：WebSocket handler 写 `sendTouch`，LVGL task 读 `indevReadCb`，单缓冲最新值覆盖
- `sendTouch()` 内置**服务端去重**：坐标+状态都没变且上条尚未消费时直接跳过
- `indevReadCb` 使用 **`hasNewData` + `continue_reading`**：
  - 有新事件 → 清标志 + `continue_reading=true` 让 LVGL 立即重读
  - 无新事件 → 维持当前按压状态 + `continue_reading=false` 空闲等待
- 由 `wsServer` 的 handler 调用 `sendTouch()` 注入触摸坐标

---

## 输入法与词库

AI 助手输入法基于 LVGL `lv_ime_pinyin`（K26 全键盘），支持**拼音 / 五笔 / 英文**三态循环切换（键盘 🔣 按钮），词库从 FAT 分区加载。

### 三态模式（`AiAgentOverlay::ImeMode`）

| 模式 | 词库 | 说明 |
|------|------|------|
| `Pinyin`（拼） | `pinyin_dict.txt` | 拼音输入，简体优先，常用字靠前 |
| `Wubi`（五） | `wubi_pinyin_dict.txt` | **五笔+拼音混合**：五笔字优先，拼音兜底 |
| `English`（EN） | — | 英文直接上屏 |

- 键盘 🔣（LV_SYMBOL_KEYBOARD/CLOSE）按钮循环切换：拼 → 五 → EN → 拼
- 切换时 `setImeMode()` 强制 `LV_IME_PINYIN_MODE_K26`（覆盖 LVGL 内部 🔣 触发的 K26/K9 翻转，K9 未编译）
- **空格上屏**：中文模式有候选时按空格上屏当前候选（无选中默认第一个），无候选放行空格
- 候选面板每页 6 字，`<`/`>` 或手柄翻页（每音候选不受 6 限制）

### 词库机制

```cpp
class PinyinDict {
    static const lv_pinyin_dict_t* loadFromFat();       // 拼音 /root/system/pinyin_dict.txt
    static const lv_pinyin_dict_t* loadWubiFromFat();   // 纯五笔（备用）
    static const lv_pinyin_dict_t* loadMixedFromFat();  // 五笔+拼音混合（"五"模式使用）
};
```

- 词库为 `lv_pinyin_dict_t { py, py_mb }` 数组（`{NULL,NULL}` 终止），`py_mb` 汉字 3 字节 UTF-8 直接连接
- **必须按字典序**（LVGL 前缀匹配返回第一个命中，短码在前）
- 加载：解析 FAT 文本 → qsort 字典序 → PSRAM 紧凑分配；失败回退 LVGL 内置词库
- `lv_ime_pinyin_set_dict()` 注入（创建 IME 后 / 切换模式时）
- 生成脚本在 `tools/`，源为 Rime（`~/.config/ibus/rime/`）：
  - `gen_pinyin_dict.py` → 从 `cn_dicts/8105.dict.yaml`（通用规范汉字表 8105 字 + 字频）提取
  - `gen_wubi_dict.py` → 从 `wubi86_jidian.dict.yaml` 提取单字
  - `gen_wubi_pinyin_dict.py` → 混合（五笔优先 + 拼音兜底去重）

### LVGL 必要 patch

- `0005`：移除 `pinyin_search_matching` 的 `i/u/v` 开头过滤（五笔 i 水 / u 立 / v 女折 键区）
- `0006`：`count` 从 `uint8_t`→`uint16_t`（五笔每组上千条目，uint8_t 截断 255 导致后段编码匹配失败）
- 详见 [Patches](#patches) 章节

---

## LVGL 锁规则

`Display::lockGuard()` 获取 LVGL 内部互斥锁。该锁是**可重入（递归）互斥锁**（`xSemaphoreCreateRecursiveMutex`），同一 task 可多次调用 `lockGuard()` 嵌套加锁而不会死锁。

以下场景需注意：

| 调用上下文 | 能否执行栈操作 / LVGL 操作 | 处理方式 |
|-----------|--------------------------|---------|
| BLE 手柄回调 (`onGamepadInput`) | ✅ 可直接调 `pushApp`/`popApp` | BLE task 不持锁 |
| 普通 Task (`Task::addTask`) | ✅ 可直接调 | 独立 task |
| LVGL 事件回调 (`lv_event_cb`) | ⚠️ 部分情况须 `Task::addTask` 延后 | LVGL task 内部分操作会导致 LVGL 异常 |

---

## AppStack — 多栈导航系统

**模块路径**: `main/app/`

| 文件 | 内容 |
|------|------|
| `app.hpp` / `app.cpp` | `App` 基类 |
| `appStack.hpp` / `appStack.cpp` | `AppStack` 单栈 |
| `appStackManager.hpp` / `appStackManager.cpp` | `AppStackManager` 多栈管理器 |

### 架构

```
AppStackManager
├── Stack 0 [Desktop]          ← 根栈（桌面，常驻，禁止 pop 到最后）
├── Stack 1 [Game, ...]       ← 游戏独立栈（pushToNewStack 创建）
└── activeStack               ← 当前前台栈
```

### App 基类

```cpp
class App {
public:
    App(Display* display);
    virtual ~App();

    // ── 基础生命周期 ──
    virtual void init();
    virtual void deinit();

    // ── 栈生命周期钩子 ──
    virtual void onForeground();   // 成为活跃 app
    virtual void onBackground();   // 被覆盖/移出

    // ── 输入 ──
    virtual void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state);

    // ── AI 能力 ──
    virtual std::string getAiContext();                         // App 上下文 → 拼入 AI prompt
    virtual void onAiSelect(const AiSelectTarget& target);     // select 焦点
    virtual void onAiActivate();                                // activate 触发
    virtual bool executeAiAction(const AiAction& action);       // 自定义动作拦截

    // ── 便利方法 ──
    void pushApp(App* app);        // push 到当前栈
    void popApp();                 // 弹出当前栈顶
    void replaceWith(App* app);    // 替换当前栈顶

protected:
    Display* display{};
    lv_obj_t* screen{};
    bool running{};
    bool deletable{ true };
    AppStackManager* m_manager{};
};
```

### 核心规则

- **根栈 depth ≤ 1 禁止 pop** — 防止桌面被弹出
- **非根栈 pop 后为空 → 自动切回根栈 + 销毁空栈**
- **push 不删旧 app** — 旧 app 留在栈底，等 pop 才删
- **异步删除** — `scheduleDeletion()` 在独立 Task 中 `deinit()` → 等 `deletable` → `lockGuard` → `delete`
- **LVGL 事件回调中不可执行栈操作** — 须用 `Task::addTask` 延后（因 LVGL 锁已被持有）
- **AppStack 操作须手动去重** — AppStack 不提供内置防重入保护，`popApp()` / `replaceWith()` / `pushToNewStack()` 等操作在快速连续触发时会导致重复执行（如双击"重新开始"创建两个实例）。**所有 AppStack 操作的入口点必须手动实现防重逻辑**。

### 去重模式

```cpp
// 1. 在类中声明时间戳字段
TickType_t m_nextActionTime = 0;
static constexpr TickType_t ACTION_COOLDOWN_MS = 500;

// 2. LVGL 事件回调中加去重检查
void onRestartCb(lv_event_t* e) {
    auto* self = static_cast<MyApp*>(lv_event_get_user_data(e));

    if (xTaskGetTickCount() < self->m_nextActionTime) {
        ESP_LOGI(TAG, "多次点击，已过滤");
        return;
    }
    self->m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN_MS;

    Task::addTask([](void* p) -> TickType_t {
        static_cast<MyApp*>(p)->replaceWith(new MyApp(...));
        return Task::infinityTime;
    }, "deferredOp", self, 0, Task::Affinity::None);
}

// 3. onForeground 中设初始延迟，防止恢复前台时误触
void onForeground() override {
    m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN_MS;
}
```

### 使用方式

```cpp
class MyApp : public App {
public:
    MyApp(Display* display) : App(display) {}

    void init() override {
        App::init();
        // 创建 LVGL 对象（持锁状态下）
        if (auto guard = display->lockGuard()) {
            lv_label_create(screen);
            // ...
        }
    }

    void deinit() override {
        // 停止后台线程
        App::deinit();
    }

    void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override {
        if (state.isPressed(GamepadButton::BTN_A))
            pushApp(new NextApp(display));   // ✅ 直接调用（BLE task）
        if (state.isPressed(GamepadButton::BTN_B))
            popApp();                        // ✅ 直接调用（BLE task）
    }
};

// LVGL 事件回调中调用需延后，否则LVGL内部会出现异常（display内的元素在意料之外变化过大）
static void on_click_cb(lv_event_t* e) {
    auto* self = static_cast<MyApp*>(lv_event_get_user_data(e));
    Task::addTask([](void* p) -> TickType_t {
        static_cast<MyApp*>(p)->pushApp(new NextApp(p->display));
        return Task::infinityTime;
    }, "deferred", self, 0, Task::Affinity::None);
}
```

### 状态机

| 状态 | 说明 |
|------|------|
| `running` | 应用是否正在运行 |
| `deletable` | 应用是否可被删除（析构时需等待此标志） |

### 生命周期

1. **构造**: 简单构造，`deletable = true`
2. **init()**: 标记 `running = true`，`deletable = false`，可在此执行耗时初始化
3. **onForeground()**: app 成为栈顶，可启动定时器/传感器
4. **onBackground()**: app 被覆盖，暂停耗时任务
5. **deinit()**: 标记 `running = false`，`deletable = true`，停止后台线程
6. **析构**: 等待 `deletable == true` 后，持 LVGL 锁删除 screen 对象

---

## 应用层

所有 App 继承自 `App` 基类，位于 `main/app/` 下。

### 桌面

| App | 路径 | 说明 |
|-----|------|------|
| **DesktopApp** | `main/app/desktopApp/` | 桌面启动器，7 游戏卡片网格，3 焦点组（卡片/底部按钮/状态栏），手柄 4 方向导航 |

按钮映射：
| 操作 | 按键 |
|------|------|
| 切换卡片 | ← → |
| 开始游戏 / 打开设置 | A |
| 上一步 / 返回桌面 | B |
| 切换焦点组 | ↑ ↓ |

### 游戏

| App | 路径 | 说明 |
|-----|------|------|
| **TetrisApp** | `main/app/tetris/` | 俄罗斯方块。Host-authoritative，3 玩家（内置 P0 + 2 WebSocket 远程），共享 PieceQueue 7-bag |
| **FruitNinjaApp** | `main/app/fruitNinja/` | 水果忍者。含房间系统（FruitNinjaRoom），多玩家。渲染 canvas 化（单 lv_canvas + 脏区失效，见模块详情） |
| **Snake** | `main/app/snake/snakeGame/` | 贪吃蛇。snakeGame 逻辑 + snakeRoom 房间配对 |
| **ChineseChess** | `main/app/chineseChess/` | 中国象棋。含 AI 引擎 (`chessAI.cpp`)，双人对战 |

### AI Agent

| App | 路径 | 说明 |
|-----|------|------|
| **AiAgentOverlay** | `main/app/aiAgentApp/` | AI 助手浮动窗（`lv_layer_sys`）。不占 AppStack，对话历史 + 长期记忆。可拖拽，顶部下滑/桌面卡片/手柄组合键唤起。集成 LVGL `lv_ime_pinyin` 输入法（三态：拼/五/EN，自定义词库 + 空格上屏） |

**模块路径**: `main/aiAgent/`

| 模块 | 说明 |
|------|------|
| `aiProtocol.hpp` | `AiActionType` / `AiAction` / `AiResponse`（含 `memory` + `nextRound`） |
| `aiConfig.hpp` | FAT 配置文件 + NVS API Key 加载 |
| `aiClient.hpp` | `HttpClient::send()` / `sendWithHistory()` — `esp_http_client` HTTPS POST |
| `aiParser.hpp` | cJSON 解析，鲁棒处理；提取 `actions[]`、`memory`、`nextRound` |
| `aiExecutor.hpp` | 动作顺序执行，13 个系统操作（含 Select/Activate/PowerOff 等） |
| `aiSystemInfo.hpp` | 系统信息采集：电池、WiFi、存储、内存、手柄电量等 |

**AiAction 字段**（所有系统操作共用）：

| 字段 | 类型 | 用途 |
|------|------|------|
| `action` | string | 操作类型（必填） |
| `text` | string | 字符串参数（app名、焦点组名、回复内容等） |
| `value` | number | 数值参数（亮度、音量、焦点索引等） |
| `params` | object | 扩展参数（复杂 JSON，如 wifi_connect 的 ssid/password） |

**AiResponse 顶层字段**：

| 字段 | 类型 | 用途 |
|------|------|------|
| `actions` | array | 操作列表 |
| `memory` | string (可选) | 长期记忆，增量更新 |
| `nextRound` | bool (可选) | true=本轮执行后继续下一轮（如 App 切换后探索新界面） |

**App 基类 AI 接口** (`main/app/app.hpp`)：

```cpp
struct AiSelectTarget {
    std::string group;   // 焦点组名（空=单维度焦点）
    int index{};         // 组内索引
};

class App {
    // 上下文描述 → 拼入 {app_context}
    virtual std::string getAiContext();

    // 焦点系统（系统级，由 AiExecutor 路由）
    virtual void onAiSelect(const AiSelectTarget& target);   // select 动作
    virtual void onAiActivate();                              // activate 动作

    // 自定义动作拦截（可选覆盖）
    virtual bool executeAiAction(const AiAction& action);
};
```

所有带焦点导航的 App 均已接入 `onAiSelect()` + `onAiActivate()`：
- DesktopApp、BleSettingsApp、WifiSettingsApp、TimeSettingsApp
- PowerManagementApp、TetrisRoomApp
- FruitNinjaRoom、SnakeRoom、ChineseChessRoom

**Agent 多轮协作流程**：

```
用户输入
  ↓
① HttpClient::send(systemPrompt, userInput, history)
  ↓
② AiParser::parse(response) → AiExecutor::execute(actions)
  ↓
③ 如果 response.nextRound == true 且未达 3 轮上限：
   → 从模板重建 system prompt（刷新 {app_context} 为新 App 信息）
   → 再次调用 AI 继续完成目标
   ↓ (最多 5 轮)
④ 记录 memory（长期记忆） + 对话历史（最近 5 轮）
  ↓
⑤ 等待用户下一轮输入
```

**13 个系统操作**：

| 操作 | 说明 | 示例 |
|------|------|------|
| `respond_text` | 回复文字 | `{"action":"respond_text","text":"你好"}` |
| `open_app` | 打开应用 | `{"action":"open_app","text":"tetris"}` |
| `select` | 移动焦点到元素 | `{"action":"select","text":"cards","value":2}` |
| `activate` | 触发当前焦点 | `{"action":"activate"}` |
| `navigate_back` | 返回上一级 | `{"action":"navigate_back"}` |
| `navigate_home` | 回桌面 | `{"action":"navigate_home"}` |
| `set_brightness` | 调亮度 0-100 | `{"action":"set_brightness","value":50}` |
| `set_volume` | 调音量 0-100 | `{"action":"set_volume","value":70}` |
| `play_audio` | 播放音频 | `{"action":"play_audio","text":"/path/file.mp3"}` |
| `wifi_connect` | 连接 WiFi | `{"action":"wifi_connect","params":{...}}` |
| `system_info` | 获取设备信息 | `{"action":"system_info"}` |
| `power_off` | 关机 (Deep-sleep) | `{"action":"power_off"}` |

**支持**：对话历史（最近 5 轮）+ 长期记忆（AI 通过 `memory` 字段增量更新）+ 多轮协作（`nextRound` 控制）+ App 焦点探索（`select`/`activate`）

**手柄按钮映射**（键盘显示时）：

| 操作 | 按键 |
|------|------|
| 键盘按键导航 | ← → ↑ ↓（左摇杆） |
| 确认按键/选候选字 | A / L3 |
| 退格删除 | B |
| 发送消息 | Y |
| 键盘/候选面板间切换焦点 | X |
| 中英文模式切换 | 键盘底部 `🔣` 按钮 |
| 候选面板翻页 | ↑ ↓（焦点在候选面板时） |

### 设置

| App | 路径 | 功能 |
|-----|------|------|
| **BleSettingsApp** | `main/app/bleSettingsApp/` | BLE 手柄扫描/连接/断开/删除配对，NVS 持久化 |
| **WifiSettingsApp** | `main/app/wifiSettingsApp/` | WiFi AP 扫描/连接/断开，密码输入键盘 |
| **TimeSettingsApp** | `main/app/timeSettingsApp/` | 手动调时 / SNTP 自动同步 / 时区设置 |
| **PowerManagementApp** | `main/app/powerManagementApp/` | 主机+手柄电量显示 / 关机 (Deep-sleep) / 低功耗 |

### 其他

| App | 路径 | 说明 |
|-----|------|------|
| **LuaManagerApp** | `main/app/luaManagerApp/` | Lua 脚本管理器：浏览/运行/删除/重命名脚本，AI 生成入口 |
| **MarketApp** | `main/app/marketApp/` | 游戏市场：截图卡片网格 + 详情 + 下载运行/本地运行 |
| **TestApp** | `main/app/testApp/` | 测试用，开发调试 |

### Tetris 模块详情

**路径**: `main/app/tetris/`

| 目录 | 内容 |
|------|------|
| `gameLogic/` | 游戏核心逻辑：`PlayerState`（单个玩家状态）、`TetrisClient`（输入/状态管理）、`GameState`（序列化快照） |
| `renderer/` | LVGL canvas 渲染器 (`tetris_renderer.cpp`)，棋盘=1 个 `lv_canvas` + 预览=4 个 `lv_canvas`（PSRAM RGB565 缓冲 + 局部失效），将 GameState 绘制到屏幕 |
| `net/` | WebSocket 网络层 (`tetris_net.cpp`)，Host/Client 模式，JSON 消息协议 |
| `net/tetrisTcpHost.hpp/.cpp` | **TCP 游戏服务器**（端口 8083），异步 send Task 20fps，主机→从机 snapshot |
| `net/tetrisRemoteClient.hpp/.cpp` | **Tetris 远程客户端**，listen 端口 8084 等待主机连接，recv Task 解析 snapshot |
| `tetrisApp.cpp` | 主游戏 App |
| `tetrisRoomApp.cpp` | 房间选择 App（含远程模式跳转） |
| `tetrisRemoteApp.cpp` | **远程游戏 App**，全屏渲染远程主机画面 |

**渲染性能设计**（TetrisRenderer canvas 化）：
- 棋盘/预览**不使用逐格 LVGL 对象**：原 220（棋盘）+ 64（预览）个 cell 对象在锁块消行时批量样式更新，会使 LVGL 失效缓冲 `LV_INV_BUF_SIZE=32` 溢出 → **整屏软件重绘** → 卡顿。
- 现改为：棋盘 = 1 个 `lv_canvas`（PSRAM RGB565 缓冲），预览 = 4 个 `lv_canvas`；方块/ghost 直接画进棋盘缓冲（`paintBoardCell`，ghost 用 `blend565` 30%/50% 混合）；`syncState` 用 `lv_obj_invalidate_area` 只失效变化区域（锁块时 5~8 条失效 < 32），日常移动仍是局部绘制 + 局部失效。
- **⚠ 坑**：`lv_obj_invalidate_area(obj, area)` 的 `area` 是**屏幕坐标**（非对象本地坐标），需先用 `lv_obj_get_coords` 换算（见 `invalidateBoardLocal`）；`lv_obj_invalidate(obj)` 整对象失效自动用正确 coords。
- 完整实现见 `main/app/tetris/renderer/tetris_renderer.hpp/.cpp`。

**消息协议** (JSON 文本帧, `ws://{host}:8080/ws/tetris` 或 TCP `host:8083` / `client:8084`):
| type | 方向 | 说明 |
|------|------|------|
| join | C→S | 加入游戏 |
| join_ack | S→C | 分配 player_id, player_count |
| input | C→S | 按键事件 |
| snapshot | S→C | 完整 GameState |

**跨主机联机流程**：
```
主机 TetrisApp::init()
  ├── tetrisNetInit(true, ...)      — 启动 WS 服务器 :8080/ws/tetris（Web 客户端）
  ├── tetrisTcpHostStart(8083)       — TCP 服务器 + 异步 send Task（20fps）
  ├── tetrisTcpHostSetPlayerCount()  — 通知客户端游戏人数
  └── tetrisTcpHostConnectToClient(ip, 8084)
       └── TCP 连接从机 → 分配 s.clients[] slot + recv Task（接收 join/input）

从机 TetrisRemoteApp::init()
  ├── m_client.listen(8084)          — 监听等待主机连接
  └── accept → sendJoin() → 收到 snapshot → TetrisRenderer::syncState()
```

### FruitNinja 模块详情

**路径**: `main/app/fruitNinja/`

| 文件 | 内容 |
|------|------|
| `fruitNinjaLogic.hpp/.cpp` | 游戏核心逻辑（纯 C++，无 LVGL）：水果物理/切片/粒子/方向+按钮匹配 |
| `fruitNinjaRenderer.hpp/.cpp` | **canvas 化渲染器**：单 `lv_canvas` 软件绘制 + 脏区集合失效（游戏层零 LVGL 对象） |
| `fruitNinjaApp.cpp` | 主游戏 App（游戏循环 60fps + 多指触摸轮询 GT911Indev 槽位） |
| `fruitNinjaRoom.cpp` | 房间系统（多玩家配对） |

**渲染性能设计**（FruitNinjaRenderer canvas 化）：
- **根因**：原对象池模式每帧 ~300+ LVGL 对象操作（40 水果 lv_image + 40 粒子 + ≤128 刀光段×2 层 lv_line），
  累计失效区域超过 `LV_INV_BUF_SIZE=256`（`lv_refr.c:332` 溢出即写整屏）→ 整屏光栅化 + 水果 shadow 模糊 → 游戏线程 CPU ~20%。
- **现架构**：游戏层（水果+粒子+分隔线+刀光+提示文字）画进**单个 `lv_canvas`**（1280×720 RGB565 PSRAM ~1.76MB），
  **游戏层零 LVGL 对象**：
  - 直接操作 canvas buffer 指针（**勿用 `lv_canvas_set_px`**——逐像素 invalidate 陷阱，见 canvas.h 文档）
  - 软件绘制：`fillRect`（首行填充 + 后续行 memcpy 复制，内存带宽级）、`fillCircle`（扫描线：行内求半宽直接填区间 + 顶部高光按行 blend）、
    `blitImage`（水果图片圆形裁剪 + 最近邻缩放 blit）、`drawWideLine`（点到线段距离圆头线）、`blend565`
  - **脏区集合失效**：`collectDirty`（变化对象 上帧bbox∪本帧bbox，精确不合并；水果 bbox 含顶部提示区域 56×36px）
    → `expandRedraw`（**与 D 相交一次遍历，无 O(n²) 传播**——paint 按 z 序从底到顶绘制，被覆盖对象天然正确）
    → `paint`（清 D → 按 z 序重画 R：水果→粒子→分隔线→刀光→提示文字）
    → 对 D 逐区域 `lv_obj_invalidate_area`（屏幕坐标换算，参考 tetris `invalidateBoardLocal`）
  - **LVGL 内置 `lv_refr_join_area`(lv_refr.c:618) 免费做自适应合并**：相邻且合并面积有利的区域自动合并成簇，分散区域保持精确小面积（无需应用层 AABB 合并，避免虚胖）
  - 首帧强制整 canvas 失效（`m_initialInvalidated`）
- **帧前/提交分离（收缩锁范围）**：`beginFrame()`（无锁：collectDirty + expandRedraw，纯计算）在 `gameLoop` 无锁区执行；
  `render()`（持锁：paint 写 buffer + invalidate + HUD）→ 持锁窗口最小化，减少与 LVGL 刷新的锁竞争
- **提示文字（↑A/↓B）**：↑↓←→ + A/B/X/Y 共 8 字符 glyph 预缓存——`create()` 持锁用 `lv_font_get_glyph_dsc`/`lv_font_get_glyph_bitmap`
  取 A8 位图，按 `header.stride` 逐行复制成紧凑 PSRAM 私有副本；每帧**无锁**软件 blit（定位公式与 LVGL 一致 `lv_draw_label.c:624`）。
  ⚠ freetype IMAGE 模式 `lv_font_get_glyph_bitmap` 返回 `lv_draw_buf_t*`（**非像素数组**）、位图 stride 对齐——不能直接 memcpy（会乱码）
- **刀光**：段池（kMaxSegments=64）+ **超时消失**（`TRAIL_LIFETIME=0.5s`，`ageTrail` 递减 life），纯色不透明单层粗线（无逐帧淡出/半透明，性能最优）；仅在新增/移除/回绕顶掉时标脏
- **水果**：图片化——`esp_driver_jpeg` 硬件解码 5 张 JPG（`JPEG_DECODE_OUT_FORMAT_RGB565` + 小端序，无需 RGB888→565 转换）
  → PSRAM（>128px 边软件最近邻缩到 128，省内存 + 加速 blit）→ `blitImage` 软件圆形裁剪 + 最近邻缩放 blit；
  任一图失败自动回退纯色圆 + 顶部高光。⚠ buffer 必须用 `jpeg_alloc_decoder_mem` 分配（驱动 DMA 对齐要求，
  `heap_caps_malloc` 普通分配会报 `not aligned`）；`jpeg_decoder_process` 的 outbuf_size 传 `allocated_size`
- **移除全部 shadow**（模糊重算是性能杀手）
- HUD / 退出按钮 / GameOver 遮罩 保留 LVGL 对象（低频/轻量）
- **性能实测**：游戏线程 CPU 47%→~7%（5a：提示标签进 canvas + 去 expandRedraw O(n²) + fill 优化 + 收缩锁范围），
  普通 FPS ~31（目标 30 达标）、CPU ~35%；极端情况（大量粒子+刀光）FPS ~10，剩余瓶颈 = `paint` 写大脏区（fruitNinja 49%），
  可选项：粒子/刀光数量上限、Phase 5b 双缓冲并行（把 paint 也挪出锁）
- 完整实现见 `main/app/fruitNinja/fruitNinjaRenderer.hpp/.cpp`

---

## 网络层

### WiFi — STA/AP 双模

**模块路径**: `main/wifi/wifi.hpp`

C 风格函数 API，非类封装。

| 函数 | 说明 |
|------|------|
| `wifiInit(bool remote)` | 初始化 WiFi 栈，`remote=true` 通过 C6 实现 |
| `wifiStart()` / `wifiStop()` | 启动/停止 WiFi 驱动 |
| `wifiStationStart/Stop()` | STA 模式控制 |
| `wifiApStart/Stop()` | AP 模式控制（默认 SSID: `esp32p4`） |
| `wifiConnect(ssid, pass)` | 连接 WiFi |
| `wifiStationScan()` | 同步扫描 AP 列表 |
| `wifiNatStart/Stop()` | NAT 转发控制（STA→AP 客户端） |

**启动策略**:
1. 尝试 STA 模式连接保存的 WiFi
2. 连接失败 → 启动 AP 模式（SSID: `esp32p4`）
3. NAT 自动将 STA 网络共享给 AP 客户端

### C6 Slave — 协处理器管理器

**模块路径**: `main/wifi/slave.hpp`

```cpp
class Slave {
    static Slave& instance();
    void start();        // 后台 Task 中早期启动 SDIO 复位 (~1.5s)
    void waitReady();    // 阻塞等待协处理器就绪
    bool isReady() const;
};
```

- `start()` 在 `app_main()` 早期（NVS 初始化后）通过 `Task::addTask` 后台执行 SDIO 复位
- 与显示/LVGL 初始化并行，节省 ~1.5s 启动时间
- 内部使用 `EventGroup` (`kReadyBit`)，支持多 Task 并发等待
- `BleGamepad::initEspHostedBt()` 中的第二次调用因 transport 已 up 而立即返回

### SocketStream — TCP 流封装

**模块路径**: `main/wifi/socketStream.hpp`

| 类 | 说明 |
|------|------|
| `ISocketStream` | TCP 输入流：`get()`, `peek()`, `read()`, `getline()` |
| `OSocketStream` | TCP 输出流：`put()`, `print()`, `sendNow()` |
| `IOSocketStream` | 同时继承输入+输出流 |

内部维护 64 字节输入缓冲。

### SocketStreamWindow — 连接池

**模块路径**: `main/wifi/socketStreamWindow.hpp`

```cpp
class SocketStreamWindow {
    bool setSocket(Socket socket);    // 占用一个窗口
    IOSocketStream& getSocketStream();
    bool closeSocket(bool blocked);   // 释放窗口
    static size_t getEnabledCount();  // 当前活跃连接数
};
```

HTTP 服务器使用 6 个窗口作为工作者线程池。

### HTTP Server

**模块路径**: `main/server/`

| API | 说明 |
|------|------|
| `serverStart(maxRetry=3)` | 启动 HTTP 服务器（裸 socket, 端口 80, 6 workers） |
| `serverStop()` | 停止服务器 |
| `serverIsStarted()` | 检查运行状态 |

- 基于 `SocketStreamWindow` 连接池，6 个工作者线程从池中取连接处理
- 内置 HTML 文件服务（`buildinHtml/`）
- 静态资源目录：`/root/`（FAT 分区）

### WS Server — WebSocket 服务器

**模块路径**: `main/wsServer/`

| API | 说明 |
|------|------|
| `wsServerStart()` | 启动 WebSocket 服务器（端口 8080） |
| `wsServerStop()` | 停止 |
| `wsServerRegisterWs(uri, handler)` | 注册 WebSocket handler |
| `wsServerUnregister(uri)` | 注销 |
| `wsServerSendText(fd, data, len)` | 发送文本帧到指定客户端 |
| `wsServerBroadcastText(data, len)` | 广播文本帧到所有客户端 |

**配置**: `max_uri_handlers = 8`, `max_open_sockets = 8`, 单 worker task (RealTime 优先级)

**注册机制**: 各游戏模块自行注册 handler（如 Tetris: `/ws/tetris`），不修改 wsServer 代码

### mDNS

**模块路径**: `main/wifi/mdns.hpp`

| API | 说明 |
|------|------|
| `mdnsInit()` / `mdnsDeinit()` | 初始化/反初始化 |
| `mdnsStart(hostname, instance)` | 设置主机名和实例名（默认 `"esp32p4"`） |
| `mdnsServiceAdd(name, "_http", "_tcp", 80)` | 注册 HTTP 服务 |
| `mdnsServiceAdd(name, "_ws", "_tcp", 8080)` | 注册 WS 服务 |
| `mdnsServiceAdd(name, "_p4console", "_tcp", 80)` | 注册专属服务类型，供设备发现用 |
| `mdnsGetConfiguredHostname()` | 从 NVS 读取持久化的主机名，未配置返回 `"esp32p4"` |
| `mdnsSetHostname(hostname)` | 保存到 NVS 并热重启 mDNS（deinit→init→重注册所有服务） |

**主机名自定义**：
- NVS 持久化（namespace `"mdns"`，key `"hostname"`）
- 修改后立即生效，无需重启
- 服务实例名自动跟随派生：`{hostname} HTTP` / `{hostname} WS`
- 主机名校验：仅字母、数字、连字符，最长 48 字符
- 配置入口：WiFiSettingsApp 底部"主机名"行 / HTTP API `GET/POST /api/mdnsConfig`

### Discovery — 设备发现

**模块路径**: `main/discovery/`

```cpp
struct DiscoveredConsole { char hostname[64]; char ip[16]; uint16_t port; };

int discoverConsoles(DiscoveredConsole* consoles, int maxCount, uint32_t timeoutMs = 3000);
```

- 查询 `_p4console._tcp` 服务，返回局域网内其他游戏主机的 hostname、IP、端口
- 自动过滤自身（对比当前活跃 hostname）
- 同步阻塞请求

### ConsoleManagerApp — 多主机管理

**模块路径**: `main/app/consoleManager/`

- 通过 mDNS 发现其他主机，列表展示（hostname + IP:port）
- 桌面状态栏图标（索引 6）进入
- 支持手动扫描
- **BLE 配对风格界面**：4 槽位 + P0-P3 掩码按钮 + 交换/断开
- **访问/从机双视图**：连接后自动切换
- **每槽掩码控制**：4 个小按钮决定该 (hostId, playerId) 对是否进入本地 App
- **交换模式**：点击 [交换] → ←→ 选择目标槽 → A 确认 → 主机交换 hostId + mask 并广播
- **活动指示**：手柄输入时槽位底色变亮（同 BleSettingsApp）
- 槽位交换同时交换 **mask**，从机通过 hostname 匹配自动更新 `m_mySlot`
- 聚焦导航：CARD → MASK → ACTIONS 三阶层级
- AI 入口：`open_app("consoleManager")`

---

## 游戏市场

从网络市场服务器浏览并下载社区 Lua 游戏脚本，一键运行。服务端与游戏内容为独立仓库 `The-Defect-Market/`（git submodule）。

**模块路径**: `main/market/`（客户端）+ `main/app/marketApp/`（UI）

### 设备端协议

| 接口 | 说明 |
|------|------|
| `GET /api/scripts` | 脚本列表（JSON，含 name/author/version/desc/tags/rating/downloads/thumb/screenshot/download） |
| `GET /games/<作者>/<游戏>/<file>` | 脚本 / 缩略图 / 大图（静态文件） |
| `POST /api/stats/download` | 下载量 +1（下载成功自动上报） |
| `GET /api/search?q=` | 按名称/作者/标签/简介搜索（服务端 SQLite） |

- 服务器地址：`/root/system/market_config.json` 的 `server_url` 字段
- `MarketClient`（`main/market/marketClient.hpp`，header-only）异步封装三个 GET + 一个 POST
- `MarketConfig`（`main/market/marketConfig.hpp`）从 FAT JSON 读服务器地址

### MarketApp — 市场 UI

| 特性 | 说明 |
|------|------|
| 入口 | 桌面第 7 张卡片「游戏市场」；AI `open_app("market")` |
| 一级界面 | 3 列截图卡片网格（缩略图懒加载串行队列，未加载显示 ★评分 占位） |
| 详情 | 大图（CONTAIN 自适应，支持横屏/拼接长图）+ 简介/版本/更新时间/下载量/评分 + 下载运行 |
| 已下载识别 | 下载存 `/root/lua/market/<id>.lua`；`testFile()` 检测 → 按钮变「运行」直接本地启动，卡片显示「已装」角标 |
| 输入 | 网格 4 向导航；A 直接进详情；最后一行 ↓ 进底部栏（刷新/详情/下载运行）；B 返回 |

---

## Screen Stream Architecture

**模块路径**: `main/screenStream/`

### 原理

`ScreenStream` 单例类。直接读取 bridge 层的帧缓冲，零额外渲染、零额外拷贝。

```
LVGL 渲染 → bridge flush → 帧完成
  → on_frame_ready(disp, disp_fb, size, ctx)    ← 每帧触发一次
                                ↓
ScreenStream::frameReadyCallback:
  → 缓存 fb 指针

调用方（HTTP handler / 定时器）:
  → ScreenStream::captureJpeg(buf, size)
  → jpeg_enc_process(DMA 读 PSRAM → 编码)      ← 硬件 JPEG，0 拷贝
```

### 关键设计

| 特性 | 说明 |
|------|------|
| **零额外渲染** | 读取 bridge 已有的 `disp_fb`（front buffer），无需 snapshot |
| **零额外拷贝** | JPEG 编码器 DMA 直读帧缓冲，不经过 CPU |
| **零开销空闲态** | 串流关闭时，bridge 中仅 1 次 null 指针检查/帧 |
| **PSRAM + DMA** | DMA2D/PPA 写入和 JPEG 编码器读取都走 DMA，绕过 CPU 缓存 |
| **旋转兼容** | `disp_fb` 已由 bridge 旋转到物理方向（720×1280） |
| **线程安全** | 回调只写指针，`captureJpeg()` 用普通读取，对齐指针在 RISC-V 上是原子的 |

### Bridge 补丁

ScreenStream 依赖 `esp_lvgl_adapter` 的自定义修改，以 patch 形式分发。详见 [Patches 章节](#patches)。

### 调用方式

```cpp
ScreenStream::instance().start(&display, 720, 1280);
ScreenStream::instance().captureJpeg(buf, size);
ScreenStream::instance().stop();
```

---

## 媒体与外设

### Audio — 音频子系统

**模块路径**: `main/audio/`

#### ES8311 编解码器

```cpp
class ES8311 {
    struct Config {
        GPIO i2s_mck, i2s_bck, i2s_ws, i2s_dout;  // I²S 引脚
        GPIO pa_pin;                                 // 功放使能
        bool pa_reverted;
        bool use_mclk = true;
        uint8_t codec_addr = 0x30;
    };
    bool init(IIC& iic, Config config);
    bool open(esp_codec_dev_sample_info_t* fs);
    bool play(const void* data, int len);
    void setVolume(int percent);
};
```

引脚配置：
| 信号 | GPIO |
|------|------|
| I²S MCLK | 13 |
| I²S BCK | 12 |
| I²S WS | 10 |
| I²S DOUT | 9 |
| PA 使能 | 53 |

#### Audio Manager — 音频管理器

**`Audio`** — Meyer's Singleton，管理 `esp_audio_render` 多流混音渲染器。

**`AudioHandle`** — 值类型，效仿 `std::thread` 生命周期模型。

```cpp
// 绑定模式（handle 析构时停止播放）
AudioHandle bgm = Audio::play("/root/music/bgm.mp3");
bgm.setLoop(true).setVolume(0.8).play();

// detach 模式（解绑生命周期）
Audio::play("/root/sfx/click.wav").setVolume(0.5).detach();
```

**依赖组件**: `esp_codec_dev`, `esp_audio_codec`, `esp_audio_render`, `gmf_audio`, `esp_audio_effects`

**数据流**:
```
File (FAT) → encBuf → [esp_audio_dec_process] → pcmBuf → [*volume] → stream_write
                                                                         ↓
                                                               Mixer Thread
                                                                         ↓
                                                               outWriter → ES8311
```

**主音量**: `Audio::setMasterVolume(int)` 委托到 ES8311 硬件，NVS 持久化。

#### ES8311 电源管理

通过 render 的 `OPENED`/`CLOSED` 事件回调自动管理 ES8311 开关：
- 首个流打开 → `ES8311::open()`（DAC 上电）
- 末个流关闭 → `ES8311::close()`（DAC 断电）
- 使用 `Mutex` 保护 `open/close` 并发

```cpp
// main.cpp 不再需要手动 audio.open(&config)
audio.init(iic, config);                 // 仅初始化 I2S + codec 接口
audio.setVolume(100);
Audio::instance().init(audio);           // render 通过 event_cb 自动开关
```

#### AudioSample — 预解码缓存

将音频文件预解码为 PCM 存入 PSRAM，播放时**零 SD I/O、零解码**。

```cpp
class AudioSample {
    bool load(const char* path);         // AAC/MP3 → PCM (PSRAM)
    AudioHandle play(bool loop = false); // 从缓存播放
    int  activeCount() const;            // 当前播放实例数（应用层限流）
    void _releaseInstance();             // 内部：播放结束回调
};
```

**重采样到 48000Hz**：`load()` 解码后自动用 `esp_ae_rate_cvt` 重采样到 48000Hz，播放时 render 的 per-stream rate_cvt 检测到 `src==dest` 进入 **bypass 零缓冲**模式，数据直通 ringbuffer。

```cpp
// 一次加载，多次播放（独立流，共享 PCM）
static AudioSample s_sfx;
if (!s_sfx.isValid()) s_sfx.load("/root/sd/sfx/click.aac");

// 限流示例：限制 3 个实例
if (s_sfx.activeCount() < 3)
    s_sfx.play().detach();
```

#### 自适应限速

PCM 缓存模式下 `memcpy` 极快（~0.01ms），远超 mixer 消费速度。按实时播放时长做自适应限速：

```cpp
uint32_t paceMs = batch * 1000 / pcmSampleRate;   // 这批应播时长
uint32_t elapsed = (esp_timer_get_time() - tick) / 1000;
if (paceMs > elapsed + 1)
    vTaskDelay(pdMS_TO_TICKS(paceMs - elapsed - 1));
```

**注意**：render 的 ringbuffer `acquire_write` 有已知 bug（源码 TODO 注释），实际不阻塞。多流模式下必须靠上层限速。

#### 流管理与限流

| API | 返回值 | 用途 |
|-----|--------|------|
| `Audio::usedStreamCount()` | 总活跃流数（含 BGM） | 系统级限流 |
| `sample.activeCount()` | 特定 AudioSample 实例数 | 采样级限流 |
| `MaxStreamNum = 14` | 硬上限 | `uint16_t streamUsedMask` |

`activeCount` 在 `play()` 时原子递增，task 退出时自动递减，线程安全。

#### 已知坑

| 问题 | 说明 |
|------|------|
| **render acquire_write 不阻塞** | `esp_audio_render.c` 中 `write_rb` 的 TODO 注释承认逻辑有误，多流模式下 ringbuffer 满时不阻塞，数据静默丢弃。PCM 模式需上层自适应限速 |
| **NRVO 析构误 stop** | `Audio::play(AudioSample&)` 返回局部 handle，NRVO 未发生时拷贝构造返回值，局部 handle 析构触发 `stop()`。**修复**：`handle.detach()` 后再返回 |
| **rate_cvt flush** | `esp_ae_rate_cvt_process(nullptr, 0)` 触发 `Invalid parameter` 错误，虽然不影响数据但日志难看。应省略 flush 步骤 |

### BLE Gamepad — 无线手柄

**模块路径**: `main/bleGamepad/`

#### 架构

```
ESP32-P4 (NimBLE host)
    ↕ esp_hosted (SDIO)
ESP32-C6 (NimBLE controller + 蓝牙射频)
    ↕ BLE
ESP32-C6 手柄 (x4 max)
```

#### 关键类

```cpp
class BleGamepad {
    static BleGamepad& instance();
    bool start(Display* display);
    void startScan();
    void stopScan();
    void connect(uint8_t scanIndex);
    void disconnect(uint8_t playerId);
    void disconnectAll();
    uint8_t connectedCount() const;
    uint8_t getBatteryLevel(uint8_t playerId) const;
    void syncPairedToNvs();
    std::vector<PairedDevice> getPairedDevices() const;
};
```

#### GamepadState

```cpp
struct GamepadState {
    uint16_t buttons;        // GamepadButton 位图
    uint8_t lx{128}, ly{128}; // 左摇杆 (0~255)
    uint8_t rx{128}, ry{128}; // 右摇杆
    uint8_t lt{0}, rt{0};    // 扳机
    uint8_t dpad{15};        // D-pad (0-7=方向, 15=松开)
    bool isPressed(GamepadButton btn) const;
};
```

#### GamepadButton 位定义

| 按钮 | 位 | 说明 |
|------|-----|------|
| BTN_A | bit 0 | A 键 (SOUTH) — 确认 |
| BTN_L3 | bit 13 | 左摇杆按下 |
| BTN_START | bit 11 | 开始 |
| BTN_SELECT | bit 10 | 选择 |

#### 手柄固件

BLE 手柄使用独立的 ESP32-C6 固件项目，通过 Git Submodule 挂载在 `gamepad/`：

> **[esp32c6HidJoystick](https://codeberg.org/loouwiit/esp32c6HidJoystick)** — ESP32-C6 BLE HID Gamepad 固件

游戏市场服务器与社区游戏内容同样以 Git Submodule 挂载在根目录 `The-Defect-Market/`：

> **[The-Defect-Market](https://codeberg.org/loouwiit/The-Defect-Market)** — 游戏市场服务器 + 社区 Lua 游戏仓库（静态内容走文件系统，动态数据走 SQLite）

```bash
git clone --recurse-submodules https://<本仓库地址>
# 或已克隆后补拉子模块：
git submodule update --init --recursive
```

手柄固件与主机通过 NimBLE GATT 协议通信，定义 `GamepadButton` 位图（见 `gamepadState.hpp`）和输入事件队列。双方需保持协议同步。

> 📖 手柄固件完整文档见 [`gamepad/readme.md`](../gamepad/readme.md)。

#### 特性

- **上限 4 玩家** (`MaxPlayers = 4`)
- **自动重连**: 启动后扫描 5 秒，自动连接 NVS 中保存的已配对设备
- **NVS 持久化**: `syncPairedToNvs()` 将 BDA+name 写入 NVS
- **输入队列**: FreeRTOS Queue 做线程安全的输入事件传递
- **Battery Level**: 通过 BLE Battery Service 读取手柄电量

#### NimBLE 注意事项

| 问题 | 说明 |
|------|------|
| `EXT_ADV` 禁用 | 使用 legacy API 时必须关闭 `CONFIG_BT_NIMBLE_EXT_ADV` |
| GAP 回调不调 `ble_gap_connect` | 须用 `Task::addTask` 延迟 1tick 执行 |
| esp-hosted 异步 `EBUSY` | `disc_cancel()` 异步 → `connect()` 返回 `EBUSY`，200ms 重试 |
| `m_scanning` 标志同步 | 手动扫描时必须同步设置标志，否则 `stopScan()` 无效 |

### Battery Manager — 电池管理

**模块路径**: `main/battery/`

```cpp
class BatteryManager {
    static BatteryManager& instance();
    bool init();
    int getPercent();         // 0~100, -1=失败
    int getVoltageMv();
    bool isCharging();
    static void startChargingAnim(lv_obj_t* label);
    static void stopChargingAnim(lv_obj_t* label);
    static const char* getIcon(int percent);    // LV_SYMBOL_BATTERY_*
    static lv_color_t getColor(int percent);   // ≥61%绿, 21~60%黄, ≤20%红
};
```

**硬件配置**: ADC1_CH6, 分压 5.1kΩ+10.2kΩ, 充电检测 GPIO23 (低电平=充电中)

**充电动画**: 300ms 定时器 ping-pong：EMPTY→1→2→3→FULL→3→2→1→...

### CPU Monitor — CPU 利用率监视器

**模块路径**: `main/monitor/`

```cpp
class CpuMonitor {
    static CpuMonitor& instance();
    void start();
    void stop();
    void setBrief(bool);     // 简略模式（仅输出总结行）
};
```

- 通过 FreeRTOS `uxTaskGetSystemState()` 差分采样
- 每秒输出各任务 CPU 占用率到串口
- 双缓冲持久分配（`m_prevSnapshot` / `m_currSnapshot`），避免每轮 malloc/free

---

## GUI 辅助类

**模块路径**: `main/gui/`

### 色板

| 名称 | 色值 | 用途 |
|------|------|------|
| `Color::BG` | `#1a1a2e` | 背景 |
| `Color::CARD` | `#2d2d3d` | 卡片背景 |
| `Color::PRIMARY` | `#0088ff` | 主色（按钮/强调） |
| `Color::SUCCESS` | `#00c853` | 成功/高电量 |
| `Color::WARNING` | `#ffa800` | 警告/中电量 |
| `Color::DANGER` | `#ff3b30` | 危险/低电量 |
| `Color::TEXT` | `#ffffff` | 正文 |
| `Color::SUBTLE` | `#888888` | 辅助文字 |

### 工厂方法

```cpp
class GUI {
    static void setBackground(lv_color_t color = Color::BG);
    static lv_obj_t* createPage(lv_obj_t* parent = nullptr);
    static lv_obj_t* createCard(lv_obj_t* parent, int32_t w, int32_t h);
    static lv_obj_t* createFlex(lv_obj_t* parent, lv_flex_flow_t flow, int32_t w = LV_SIZE_CONTENT, int32_t h = LV_SIZE_CONTENT);
    static lv_obj_t* createButton(lv_obj_t* parent, const char* text, int32_t w = 120, int32_t h = 40);
    static lv_obj_t* createTitle(lv_obj_t* parent, const char* text);
    static lv_obj_t* createSubtitle(lv_obj_t* parent, const char* text);
    static lv_obj_t* createValue(lv_obj_t* parent, const char* text);
    static lv_obj_t* createLabel(lv_obj_t* parent, const char* text);
    static lv_obj_t* createSwitch(lv_obj_t* parent);
    static lv_obj_t* createSlider(lv_obj_t* parent, int32_t w, int32_t min, int32_t max, int32_t init);
    static lv_obj_t* createProgressBar(lv_obj_t* parent, int32_t w, int32_t min, int32_t max, int32_t init);
    static lv_obj_t* createMetric(lv_obj_t* parent, const char* title, const char* value, const char* unit);
    static lv_obj_t* createProgressCard(lv_obj_t* parent, const char* title, int32_t min, int32_t max, int32_t init);
    static lv_obj_t* createMenuRow(lv_obj_t* parent, const char* text, lv_obj_t* rightWidget = nullptr);
    static lv_obj_t* createImage(lv_obj_t* parent, const void* src);
    static lv_obj_t* createImageFromFile(lv_obj_t* parent, const char* path);
};

static inline void styleCard(lv_obj_t* obj);
```

---

## 存储子系统

### FAT — Flash 分区

**模块路径**: `main/storage/fat.hpp`

| API | 说明 |
|------|------|
| `mountFlash()` | 挂载 fat 分区到 `/root/` |
| `unmountFlash()` | 卸载 |
| `formatFlash()` | 格式化分区 |
| `getSpace(&free, &total)` | 获取剩余/总空间 |
| `tree(path)` | 递归列出文件树 |
| `newFile(path)` / `removeFile(path)` | 文件 CRUD |
| `newFloor(path)` / `removeFloor(path)` | 目录 CRUD |

`FileBase` 虚基类，提供 `open()` / `close()` / `read()` / `write()` / `seek()` / `tell()` / `size()`。

### memFS — 内存文件系统

**模块路径**: `main/storage/mem.hpp`

| 参数 | 值 |
|------|-----|
| 挂载点 | `/root/mem` |
| 最大大小 | 6MB |
| 最大文件描述符 | 16 |

### SD — SD 卡

**模块路径**: `main/storage/sd.hpp`

`mountSd()` → 挂载到 `/root/sd`

### VFS 挂载点汇总

| 路径 | 存储介质 | 来源 |
|------|---------|------|
| `/root/` | FAT flash (16MB) | `mountFlash()` |
| `/root/mem` | PSRAM (6MB) | `mountMem()` |
| `/root/sd` | SD 卡 | `mountSd()` |

---

## PPA (Pixel Processing Accelerator) 配置

ESP32-P4 的 PPA 硬件加速 2D 渲染 (fill/blend/SRM)。当前配置与注意事项：

### sdkconfig

```
CONFIG_LV_USE_PPA=y                # 启用 PPA
CONFIG_LV_USE_PPA_IMG=y            # PPA 加速图片渲染
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1  # PPA 要求 SW draw unit 为 1
```

### 关键约束

| 参数 | 值 | 说明 |
|------|----|------|
| `LV_DRAW_SW_DRAW_UNIT_CNT` | 须为 1 | >1 多线程渲染时会出现不可预知的渲染错误（黑线、白线、混叠） |
| PPA fill/blend `max_pending_trans_num` | *在多线程渲染时* 需 ≥2 | 默认值太低会导致崩溃: `exceed maximum pending transactions` |
| `CONFIG_LV_CACHE_DEF_SIZE` | 建议 ≥1MB | 图片缓存大小，避免每帧重复解码 JPEG |

> PPA fill/blend 与 SRM (Scale-Rotate-Mirror) 共享同一硬件引擎，大量 blend 操作会阻塞 SRM 旋转，导致帧率下降。实测中 PPA 加速 5 张卡片渲染时，blend 排队 + SRM 等待 → `ppa_do_scale_rotate_mirror` 耗时从 ~0.7ms 飙升至 ~15ms。

### 性能陷阱

| 问题 | 原因 | 解决 |
|------|------|------|
| `CONFIG_ESP_LVGL_ADAPTER_PARTIAL_AUX_IMG_CACHE` | 每帧末尾调用 `lv_image_cache_drop(NULL)` 清空整个缓存 | **禁用**该选项，直设 `LV_CACHE_DEF_SIZE` |
| LVGL 阴影 (`shadow_width`) | 模糊运算性能影响较大 | 适度使用，或改用预渲染方式 |
| 圆角 (`radius`) | 产生裁剪路径开销，但远小于阴影 | 可使用，避免过大的 radius 值 |

---

## Patches

**路径**: `patches/`

| Patch | 文件 | 说明 |
|-------|------|------|
| `0001-esp_lvgl_adapter-frame-ready-callback.patch` | `esp_lvgl_adapter` (4 文件) | Bridge 帧完成回调 → ScreenStream 零拷贝采集 |
| `0002-esp_lvgl_adapter-ppa-max-pending-trans.patch` | `lvgl_ppa_accel_v9.c` | PPA fill/blend `max_pending_trans_num = 8`，防止队列溢出崩溃 |
| `0003-esp_driver_ppa-sr_macro_bk_ro_bypass.patch` | `esp_driver_ppa` | PPA SRM (Scale-Rotate-Mirror) macro 旁路，修复旋转时的硬件 bug |
| `0004-esp_websocket_client-expose-transport.patch` | `esp_websocket_client` (2 文件) | 暴露 `esp_websocket_client_get_transport()` 供 `wsClient.cpp` 获取 socket fd 设置 TCP_NODELAY |
| `0005-lvgl__lvgl-ime-pinyin-allow-iu-v-start.patch` | `lvgl` (`lv_ime_pinyin.c`) | 移除 IME 匹配的 i/u/v 开头过滤（五笔 i 水/u 立/v 女折 键区） |
| `0006-lvgl__lvgl-ime-pinyin-fix-count-uint16.patch` | `lvgl` (`lv_ime_pinyin.c`) | `count` 类型 `uint8_t`→`uint16_t`，修复五笔大组（>255 条目）匹配截断 |

**使用**:
```bash
bash patches/apply.sh          # 打所有补丁
bash patches/apply.sh --revert # 还原所有
```
⚠ `idf.py reconfigure` 后需重新打补丁。
⚠ `apply.sh` 需要 `IDF_PATH` 环境变量；组件目录自动识别 `espressif__<组件>`，非 espressif 发布组件（如 `lvgl__lvgl`）直接用原名。
⚠ **不要对已应用的 patch 重跑全量 `apply.sh`**（`--forward` 对行号偏移的 hunk 可能重复应用 → 重复定义编译错误）；新 patch 应单独 `patch -d <组件目录> -p1 < xxx.patch`。

---

## Startup Flow (main.cpp)

```
Task::init(2)                           ← 协程调度器
srand(esp_random())                     ← 随机种子（60s 周期）

CpuMonitor::instance().start()          ← CPU 利用率监视器
esp_event_loop_create_default()         ← 默认事件循环

// ── 存储 ──
mountFlash()                            ← FAT 分区 → /root/
mountMem()                              ← 内存文件系统 → /root/mem
mountSd()                               ← SD 卡 → /root/sd
nvsInit()                               ← NVS 初始化

// ── C6 协处理器（与显示初始化并行） ──
Slave::instance().start()               ← 后台 Task SDIO 复位 (~1.5s)

// ── 显示 ──
Display::init()                         ← LVGL 适配器
ILI9881c::init(720, 1280, fbCount)      ← MIPI DSI 面板
display.bindDisplay()                   ← 绑定面板
Touch::init(iic, ...)                   ← GT911 触摸（可选）
GT911Indev::instance().start(...)       ← 五 indev 多指桥接（取代 bindTouch）
display.start()                         ← LVGL worker task

// ── 屏幕串流 ──
ScreenStream::instance().start(&display, 720, 1280)

// ── 字体 ──
FontLoader::load("F:system/NotoSC.ttf", 24)   ← Small
FontLoader::load("F:system/NotoSC.ttf", 32)   ← Medium (Default)
FontLoader::load("F:system/NotoSC.ttf", 56)   ← Large
// 字体回退链
const_cast...->fallback = lv_font_get_default();

// ── 虚拟输入 ──
VirtualIndev::instance().start(&display)

// ── 音频 ──
ES8311::init(iic, config)               ← 编解码器初始化
Audio::instance().init(audio)           ← 音频管理器
Audio::loadVolumeFromNvs()              ← 音量恢复

// ── 电池 ──
BatteryManager::instance().init()

// ── App 导航系统 ──
AppStackManager mgr(&display)
display.setStackManager(&mgr)
mgr.createStack()                       ← 根栈（Stack 0）
mgr.push(new DesktopApp(&display))      ← 桌面应用

// ── BLE 手柄 ──
BleGamepad::instance().start(&display)

// ── 网络 ──
wifiInit(true)                          ← 初始化（通过 C6）
wifiStart()
wifiStationStart(); wifiConnect(ssid, pass)
if (!connected) wifiApStart()           ← 连接失败 → AP 模式
mdnsInit(); mdnsStart(hostname, ...)
mdnsServiceAdd("_http", 80)
mdnsServiceAdd("_ws", 8080)
mdnsServiceAdd("_p4console", 80)       ← 设备发现专用服务

// ── 服务器 ──
serverStart()                           ← HTTP 端口 80
wsServerStart()                         ← WebSocket 端口 8080
udpInputStart()                         ← UDP 端口 8082（P4→P4 手柄透传）

// ── 主循环 ──
while (true) vTaskDelay(1000);
```

agent 应查看 `main/main.cpp` 的最新代码为准。

---

## Code Conventions

- **文件名**: `camelCase` (`display.hpp`, `serverKernal.cpp`)
- **类名**: `PascalCase`，**不使用 namespace**
- **方法/变量**: `camelCase`，成员变量无统一前缀
- **头文件**: `#pragma once`
- **TAG**: `static constexpr char TAG[] = "模块名";`
- **注释**: 中文
- **错误处理**: `ESP_ERROR_CHECK()` 用于关键初始化，`return false` 用于常规错误，**无 C++ 异常**
- **内存管理**: PSRAM 是主堆 (`CONFIG_SPIRAM_USE_MALLOC=y`)
- **并发**: FreeRTOS 双核 + 自建协程调度器（部分代码的简单实现） + mutex RAII 锁 (`LockGuard`)
- **提交格式**: `<type>: <中文描述>` — 类型: `feat`, `pref`, `refactor`, `style`, `fix`等
