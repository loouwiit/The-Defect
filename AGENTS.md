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
| nvs | 16KB | NVS 键值持久化（WiFi 配置、手柄配对、音量等） |
| otadata | 8KB | OTA 启动选择数据 |
| phy_init | 4KB | WiFi/BT 物理层校准数据 |
| ota_0 | 6MB | 应用固件（单 OTA slot） |
| fat | 16MB | FAT 数据分区，挂载到 `/root/` |

## Resources

`reserces/` 目录下的文件会被上传到 FAT 分区，挂载路径为 `/root/`。

```
reserces/
├── server/          ← HTTP 服务器静态资源（端口 80）
│   ├── index.html
│   ├── index.css
│   ├── screen/      ← 屏幕串流 Web 查看器
│   ├── tetris/      ← Tetris 网页客户端
│   └── wifi/        ← WiFi 网页配置
└── system/          ← 系统文件（字体等）
    └── NotoSC.ttf   ← 全局默认字体
```

---

## Architecture — 完整模块概览

| 层次 | 模块 | 路径 | 职责 |
|------|------|------|------|
| **基础设施** | Task | `main/task/` | 协程调度器（轮询）+ Thread 封装，Priority/Affinity 管理 |
| | Mutex | `main/mutex/` | FreeRTOS `xSemaphoreCreateMutex` RAII 封装 (`Mutex` + `Lock`) |
| | GPIO | `main/gpio/` | GPIO 装饰器封装，`=` / `bool` / `int` 运算符重载，ISR 注册 |
| | IIC | `main/iic/` | I²C 主控总线 (`IIC`) + 设备封装 (`IICDevice`)，自动端口选择 |
| **显示/输入** | Display | `main/display/display.hpp` | ILI9881C 初始化 + LVGL 适配层，`LockGuard` RAII（递归锁） |
| | ILI9881c | `main/display/ili9881c.cpp` | 面板驱动（MIPI DSI），背光亮度控制 |
| | FontLoader | `main/display/font.cpp` | FreeType 字体加载，3 字号全局默认字体，VFS 驱动器 'F' |
| | Touch | `main/touch/` | GT911 驱动 (I²C)，5 点触控 |
| | VirtualIndev | `main/virtualIndev/` | 虚拟触摸输入设备（Web 触摸注入），Mutex 保护双缓冲 |
| **导航** | App | `main/app/app.hpp` | 应用基类（生命周期、栈钩子、手柄输入） |
| | AppStack | `main/app/appStack.hpp` | 单栈导航（push/pop/replace + 异步删除） |
| | AppStackManager | `main/app/appStackManager.hpp` | 多栈管理器（根栈 + 游戏独立栈 + 前台切换） |
| **应用** | DesktopApp | `main/app/desktopApp/` | 桌面启动器，5 游戏卡片，3 焦点组导航 |
| | TetrisApp | `main/app/tetris/` | 俄罗斯方块（Host-authoritative，3P，WebSocket 网络对战） |
| | FruitNinjaApp | `main/app/fruitNinja/` | 水果忍者（含房间系统，多玩家） |
| | Snake | `main/app/snake/` | 贪吃蛇（snakeGame + snakeRoom） |
| | ChineseChess | `main/app/chineseChess/` | 中国象棋（含 AI 引擎） |
| | BleSettingsApp | `main/app/bleSettingsApp/` | BLE 手柄配对/管理 UI |
| | WifiSettingsApp | `main/app/wifiSettingsApp/` | WiFi 扫描/连接 UI |
| | TimeSettingsApp | `main/app/timeSettingsApp/` | 时间/SNTP 设置 UI |
| | PowerManagementApp | `main/app/powerManagementApp/` | 电源管理（电量、关机 Deep-sleep） |
| | TestApp | `main/app/testApp/` | 测试用 App |
| **网络** | WiFi | `main/wifi/wifi.hpp` | C 风格 API：STA/AP 双模 + NAT，`esp_wifi_remote` C6 通信 |
| | C6 Slave | `main/wifi/slave.hpp` | SDIO/ESP-HOSTED 协处理器管理器，后台 Task 早期启动 |
| | SocketStream | `main/wifi/socketStream.hpp` | TCP 输入/输出/双向流封装（`ISocketStream` / `OSocketStream` / `IOSocketStream`） |
| | SocketStreamWindow | `main/wifi/socketStreamWindow.hpp` | 连接池管理器（HTTP 6 worker 线程） |
| | HTTP Server | `main/server/` | 裸 socket TCP HTTP，端口 80，6 工作者线程，内置 HTML |
| | WS Server | `main/wsServer/` | WebSocket 端口 8080，动态 URI 注册/注销，广播 |
| | mDNS | `main/wifi/mdns.hpp` | mDNS 服务发现（`_http` 80, `_ws` 8080） |
| | HTTP | `main/wifi/http.hpp` | HTTP 协议解析（Method/Status/Header 等） |
| **媒体** | ScreenStream | `main/screenStream/` | MJPEG 硬件 JPEG 编码 + HTTP 流，直接读 bridge 帧缓冲 |
| | ES8311 | `main/audio/ES8311.hpp` | ES8311 编解码器驱动（I²C + I²S） |
| | Audio | `main/audio/Audio.hpp` | 音频管理器（Meyer's Singleton）+ AudioHandle 值类型 |
| **外设** | BLE Gamepad | `main/bleGamepad/` | NimBLE + esp_hosted，4 玩家上限，NVS 持久化配对 |
| | 手柄固件 | [`gamepad/`](https://codeberg.org/loouwiit/esp32c6HidJoystick) (submodule) | ESP32-C6 BLE HID 手柄固件，独立仓库 |
| | Battery Manager | `main/battery/` | ADC 电池电量估算 + 充电检测 (GPIO23) + 充电动画 |
| | CPU Monitor | `main/monitor/` | FreeRTOS 任务利用率差分采样，串口输出 |
| **GUI** | GUI | `main/gui/` | 色板（8 色）+ 14 个工厂方法 (`createPage/Card/Flex/Button/Title/Switch/Slider` 等) |
| **存储** | FAT | `main/storage/fat.hpp` | FAT 分区 (16MB)，VFS `/root/`，文件/目录 CRUD |
| | memFS | `main/storage/mem.hpp` | 内存文件系统 (6MB max)，VFS `/root/mem` |
| | SD | `main/storage/sd.hpp` | SD 卡，VFS `/root/sd` |
| **补丁** | Patches | `patches/` | 3 个 patch：帧回调 / PPA 队列 / SRM macro bypass |

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

---

## 显示与输入

### Display — LVGL 适配层

**模块路径**: `main/display/display.hpp`

| 方法 | 说明 |
|------|------|
| `init()` | 初始化 LVGL 适配器 |
| `bindDisplay(panel, io, w, h, tearAvoid, rotation)` | 绑定 ILI9881c 面板 |
| `bindTouch(handle)` | 绑定 GT911 触摸 |
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

- 内部通过 `Mutex` 保护 Sample 双缓冲（`state` + `point` + `updated` 标志）
- `indevReadCb` 在 LVGL task 中消费缓冲数据
- 由 `wsServer` 的 handler 调用 `sendTouch()` 注入触摸坐标

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
    virtual void onGamepadInput(uint8_t playerId, const GamepadState& state);

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

    void onGamepadInput(uint8_t playerId, const GamepadState& state) override {
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
| **DesktopApp** | `main/app/desktopApp/` | 桌面启动器，5 游戏卡片网格，3 焦点组（卡片/底部按钮/状态栏），手柄 4 方向导航 |

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
| **FruitNinjaApp** | `main/app/fruitNinja/` | 水果忍者。含房间系统（FruitNinjaRoom），多玩家 |
| **Snake** | `main/app/snake/snakeGame/` | 贪吃蛇。snakeGame 逻辑 + snakeRoom 房间配对 |
| **ChineseChess** | `main/app/chineseChess/` | 中国象棋。含 AI 引擎 (`chessAI.cpp`)，双人对战 |

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
| **TestApp** | `main/app/testApp/` | 测试用，开发调试 |

### Tetris 模块详情

**路径**: `main/app/tetris/`

| 目录 | 内容 |
|------|------|
| `gameLogic/` | 游戏核心逻辑：`PlayerState`（单个玩家状态）、`TetrisClient`（输入/状态管理）、`GameState`（序列化快照） |
| `renderer/` | LVGL 渲染器 (`tetris_renderer.cpp`)，将 GameState 绘制到屏幕 |
| `net/` | WebSocket 网络层 (`tetris_net.cpp`)，Host/Client 模式，JSON 消息协议 |
| `tetrisApp.cpp` | 主游戏 App |
| `tetrisRoomApp.cpp` | 房间选择 App |

**消息协议** (JSON 文本帧, `ws://{host}:8080/ws/tetris`):
| type | 方向 | 说明 |
|------|------|------|
| join | C→S | 加入游戏 |
| join_ack | S→C | 分配 player_id |
| input | C→S | 按键事件 |
| snapshot | S→C | 完整 GameState |

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
| `mdnsStart("esp32p4", "ESP32P4 Game Console")` | 设置主机名和实例名 |
| `mdnsServiceAdd(name, "_http", "_tcp", 80)` | 注册 HTTP 服务 |
| `mdnsServiceAdd(name, "_ws", "_tcp", 8080)` | 注册 WS 服务 |

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

**使用**:
```bash
bash patches/apply.sh          # 打所有补丁
bash patches/apply.sh --revert # 还原所有
```
⚠ `idf.py reconfigure` 后需重新打补丁。

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
display.bindTouch()                     ← 绑定触摸
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
mdnsInit(); mdnsStart("esp32p4", ...)
mdnsServiceAdd("_http", 80)
mdnsServiceAdd("_ws", 8080)

// ── 服务器 ──
serverStart()                           ← HTTP 端口 80
wsServerStart()                         ← WebSocket 端口 8080

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
