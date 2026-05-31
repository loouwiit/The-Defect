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
| App | `main/app/` | 应用基类，多态设计，`Display::applyApp()` 加载 |
| Display | `main/display/` | ILI9881C 初始化 + LVGL 适配层，提供 `LockGuard` RAII |
| FontLoader | `main/display/font.cpp` | FreeType 字体加载工具，提供全局默认字体 |
| Touch | `main/touch/` | GT911 驱动 (I²C) |

## App — 应用基类

**模块路径**: `main/app/app.hpp`

### 概述

`App` 是所有应用程序的基类，采用多态设计。通过 `Display::applyApp()` 加载，管理者 LVGL screen 对象，提供初始化和清理接口。

### 设计

```cpp
class App {
public:
    App(Display* display);
    virtual ~App();

    virtual void init()   { running = true; deletable = false; };
    virtual void deinit() { running = false; deletable = true; };

protected:
    Display* display{};
    lv_obj_t* screen{};

    bool running{};
    bool deletable{ true };

    friend class Display;
};
```

### 状态机

| 状态 | 说明 |
|------|------|
| `running` | 应用是否正在运行 |
| `deletable` | 应用是否可被删除（析构时需等待此标志） |

### 生命周期

1. **构造**: 简单构造，`deletable = true`
2. **init()**: 标记 `running = true`，`deletable = false`，可在此执行耗时初始化
3. **deinit()**: 标记 `running = false`，`deletable = true`，可在此停止耗时任务
4. **析构**: 等待 `deletable == true` 后，删除 LVGL screen 对象

### 关键特性

- **构造简单**: 构造函数应轻量，复杂逻辑移至 `init()`
- **状态管理**: 通过 `running`/`deletable` 双标志控制生命周期
- **析构安全**: 析构函数等待 `deletable` 标志，确保清理完成
- **多态设计**: 派生类重写 `init()`/`deinit()` 实现具体应用逻辑
- **Display 友元**: `Display` 类可直接访问私有成员

### 使用方式

```cpp
// 派生类示例
class MyApp : public App {
public:
    MyApp(Display* display) : App(display) {}

    void init() override {
        App::init();  // 调用基类，设置 running/deletable 状态
        // 执行耗时初始化任务
    }

    void deinit() override {
        // 停止耗时任务
        App::deinit();  // 调用基类，设置 running/deletable 状态
    }
};
```

## Screen Stream Architecture
| Task | `main/task/` | 协程调度器（轮询）+ Thread 封装 |
| Server | `main/server/` | TCP HTTP 服务器（裸 socket），端口 80，6 工作者线程 |
| WiFi | `main/wifi/` | STA/AP 双模 + NAT，`esp_wifi_remote` 支持 C6 通信，mDNS 服务发现 |
| Screen Stream | `main/screenStream/` | MJPEG 硬件 JPEG 编码 + HTTP 流。直接读 bridge 帧缓冲，零额外渲染 |
| Storage | `main/storage/` | FLASH & memFS → SD |
| IIC | `main/iic/` | I²C 主控 + 设备封装 |
| GPIO | `main/gpio/` | GPIO 封装，支持 `=` 和 `bool` 运算符重载 |
| Mutex | `main/mutex/` | 互斥锁 + RAII Lock |

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
└── apply.sh
```

修改的文件：
- `src/display/ports/display_bridge.h` — 抽象接口加 `on_frame_ready` + `frame_ready_user_ctx` 字段
- `src/display/bridge/v9/lvgl_bridge_v9.c` — 7 个 flush 函数的帧完成点触发回调
- `src/display/display_manager.h` — 新增 `display_manager_set_frame_ready_callback()` API
- `src/display/display_manager.c` — 实现注册函数

**使用**:
```bash
bash patches/apply.sh          # 打补丁
bash patches/apply.sh --revert # 还原
```
⚠ `idf.py reconfigure` 后需重新打补丁。

### 调用方式

```cpp
ScreenStream::instance().start(&display);  // 启动（注册回调 + 初始化 JPEG 编码器）
ScreenStream::instance().captureJpeg(buf, size);  // 按需采集
ScreenStream::instance().stop();           // 停止
```

详细架构见 `readme.md`。

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

## Startup Flow (main.cpp) — 初期阶段，可能变动

> **注意**：项目处于早期阶段，以下启动顺序尚未定型，后续可能大幅调整。agent 应查看 `main/main.cpp` 的最新代码为准。

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
