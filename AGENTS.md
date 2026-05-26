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
| Touch | `main/touch/` | GT911 驱动 (I²C) |
| Task | `main/task/` | 协程调度器（轮询）+ Thread 封装 |
| Server | `main/server/` | TCP HTTP 服务器（裸 socket），端口 80，6 工作者线程 |
| WiFi | `main/wifi/` | STA/AP 双模 + NAT，`esp_wifi_remote` 支持 C6 通信 |
| Screen Stream | `main/screen_stream/` | MJPEG 硬件 JPEG 编码 + HTTP 流 |
| Storage | `main/storage/` | FLASH & memFS → SD |
| IIC | `main/iic/` | I²C 主控 + 设备封装 |
| GPIO | `main/gpio/` | GPIO 封装，支持 `=` 和 `bool` 运算符重载 |
| Mutex | `main/mutex/` | 互斥锁 + RAII Lock |

详细架构见 `readme.md`。

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
