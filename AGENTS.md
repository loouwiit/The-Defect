# The-Defect — Agent Guidelines

## Project Overview

ESP32-P4 驱动的多人游戏主机 + ESP32-C6 无线手柄。屏幕为 6寸 720×1280 MIPI ILI9881C，触摸 GT911 (I²C)，GUI 使用 LVGL 9.4。

## Build & Flash

使用 `@espIdfCommands` 工具（ESP-IDF 扩展）进行编译、烧录、监控等操作，不要直接执行 bash 命令。

分区表：双 OTA (2MB×2) + FAT 数据分区 (16MB)。

## Resources

`reserces/` 目录下的文件会被上传到 ESP32-P4 内置 FLASH 的 FAT 分区，挂载路径为 `/root/`。

`reserces/server/` 是 HTTP 服务器可访问的文件目录，用于存放网页静态资源。

## Architecture

| 模块 | 路径 | 职责 |
|------|------|------|
| App | `main/app/` | 应用基类 + AppStack 多栈导航系统 |
| Display | `main/display/` | ILI9881C 初始化 + LVGL 适配层，提供 `LockGuard` RAII |
| FontLoader | `main/display/font.cpp` | FreeType 字体加载工具，提供多字号全局默认字体 |
| Touch | `main/touch/` | GT911 驱动 (I²C) |

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
// 1️⃣ 在类中声明时间戳字段
TickType_t m_nextActionTime = 0;
static constexpr TickType_t ACTION_COOLDOWN_MS = 500;

// 2️⃣ LVGL 事件回调中加去重检查
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

// 3️⃣ onForeground 中设初始延迟，防止恢复前台时误触
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

## Screen Stream Architecture
| Task | `main/task/` | 协程调度器（轮询）+ Thread 封装 |
| Server | `main/server/` | TCP HTTP 服务器（裸 socket），端口 80，6 工作者线程 |
| WiFi | `main/wifi/` | STA/AP 双模 + NAT，`esp_wifi_remote` 支持 C6 通信，mDNS 服务发现 |
| Screen Stream | `main/screenStream/` | MJPEG 硬件 JPEG 编码 + HTTP 流。直接读 bridge 帧缓冲，零额外渲染 |
| Storage | `main/storage/` | FLASH & memFS → SD |
| IIC | `main/iic/` | I²C 主控 + 设备封装 |
| GPIO | `main/gpio/` | GPIO 封装，支持 `=` 和 `bool` 运算符重载 |
| Mutex | `main/mutex/` | 互斥锁 + RAII Lock |

## LVGL 锁规则

`Display::lockGuard()` 获取 LVGL 内部互斥锁。该锁是**可重入（递归）互斥锁**（`xSemaphoreCreateRecursiveMutex`），同一 task 可多次调用 `lockGuard()` 嵌套加锁而不会死锁。

以下场景需注意：

| 调用上下文 | 能否执行栈操作 / LVGL 操作 | 处理方式 |
|-----------|--------------------------|---------|
| BLE 手柄回调 (`onGamepadInput`) | ✅ 可直接调 `pushApp`/`popApp` | BLE task 不持锁 |
| 普通 Task (`Task::addTask`) | ✅ 可直接调 | 独立 task |
| LVGL 事件回调 (`lv_event_cb`) | ⚠️ 部分情况须 `Task::addTask` 延后 | LVGL task 内部分操作会导致 LVGL 异常 |

## Screen Stream Architecture

### 原理

`main/screenStream/` — `ScreenStream` 单例类。

传统方案使用 `lv_snapshot_take_to_draw_buf()` 截取屏幕，但该 API **会完整重绘 UI 对象树**到新 draw buffer，开销极大且随 UI 复杂度增长。

本项目改用 **直接读取 bridge 层的帧缓冲**：

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
| **PSRAM + DMA** | DMA2D/PPA 写入和 JPEG 编码器读取都走 DMA，绕过 CPU 缓存。不需要 `esp_cache_msync()` |
| **旋转兼容** | `disp_fb` 已由 bridge 旋转到物理方向（720×1280），JPEG 编码无需额外处理 |
| **线程安全** | 回调（LVGL 任务）只写指针，`captureJpeg()`（HTTP 任务）用普通读取，对齐指针在 RISC-V 上是原子的 |

### Bridge 补丁

ScreenStream 依赖 `esp_lvgl_adapter` 的 4 个文件中的自定义修改，以 patch 形式分发：

```
patches/
├── 0001-esp_lvgl_adapter-frame-ready-callback.patch
├── 0002-esp_lvgl_adapter-ppa-max-pending-trans.patch
└── apply.sh
```

`0001` 修改的文件：
- `src/display/ports/display_bridge.h` — 抽象接口加 `on_frame_ready` + `frame_ready_user_ctx` 字段
- `src/display/bridge/v9/lvgl_bridge_v9.c` — 7 个 flush 函数的帧完成点触发回调
- `src/display/display_manager.h` — 新增 `display_manager_set_frame_ready_callback()` API
- `src/display/display_manager.c` — 实现注册函数

`0002` 修改的文件：
- `src/display/bridge/v9/lvgl_ppa_accel_v9.c` — PPA fill/blend 客户端添加 `max_pending_trans_num = 8`，防止队列溢出崩溃

**使用**:
```bash
bash patches/apply.sh          # 打所有补丁
bash patches/apply.sh --revert # 还原所有
```
⚠ `idf.py reconfigure` 后需重新打补丁。

### 调用方式

```cpp
ScreenStream::instance().start(&display);  // 启动（注册回调 + 初始化 JPEG 编码器）
ScreenStream::instance().captureJpeg(buf, size);  // 按需采集
ScreenStream::instance().stop();           // 停止
```

详细架构见 `readme.md`。

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
| 圆角 (`radius`) | 产生裁剪路径开销，但远小于阴影 | 可使用

## FontLoader — 字体加载

**模块路径**: `main/display/font.cpp`

### 概述

`FontLoader` 是一个无状态的纯工具类，负责从 VFS 路径加载 FreeType 字体，并提供全局默认字体。

### 设计

```cpp
class FontLoader {
public:
    // 加载字体文件，返回 lv_font_t*
    // 生命周期由 esp_lv_adapter_deinit() 统一管理
    static const lv_font_t* load(const char* vfsPath);

    // 全局默认字体（供所有对象使用）
    static const lv_font_t* getDefault();
    static void setDefault(const lv_font_t* font);

private:
    FontLoader() = delete;
    ~FontLoader() = delete;

    static const lv_font_t* s_defaultFont;
};
```

### VFS 驱动器 'F'

FontLoader 在内部注册 LVGL VFS 驱动器 `'F'`，将 `"F:..."` 路径映射到 `/root/`：

```
FreeType "F:system/NotoSC.ttf"
    → LVGL VFS driver 'F'
    → convert_path: 去掉 "F:" 前缀，拼接到 "/root/"
    → fopen("/root/system/NotoSC.ttf")
    → VFS → FAT flash
```

### 使用方式

```cpp
// main.cpp — 初始化时加载并设为默认
FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf"));

// App 构造函数 — 自动继承字体到所有子对象
lv_obj_set_style_text_font(screen, FontLoader::getDefault(), 0);

// desktopApp — 直接使用默认字体，无需每次指定路径
lv_obj_set_style_text_font(label, FontLoader::getDefault(), 0);
```

### 关键特性

- **无状态**: FontLoader 不存储字体实例，每次 load() 返回新句柄
- **全局默认字体**: 通过 `setDefault()` / `getDefault()` 提供共享字体
- **样式继承**: App 基类在 screen 上设置默认字体，所有子对象自动继承
- **生命周期**: 字体由 `esp_lv_adapter_deinit()` 统一释放，无法单独 deinit

## Startup Flow (main.cpp)

```
Task::init(2)                   ← 协程调度器
Display::init()                 ← LVGL 适配器
bindDisplay()                   ← ILI9881C
bindTouch()                     ← GT911（可选）
Display::start()                ← LVGL worker task
FontLoader::load/setDefault     ← FreeType 字体

AppStackManager mgr(&display)   ← 多栈导航系统
display.setStackManager(&mgr)
mgr.createStack()               ← 根栈（Stack 0）
mgr.push(new DesktopApp(...))   ← 桌面应用

WiFi / mDNS / Server / BLE      ← 网络和外设
```

agent 应查看 `main/main.cpp` 的最新代码为准。

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
