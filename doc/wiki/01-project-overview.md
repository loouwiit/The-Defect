# The-Defect — 项目概述

## 什么是 The-Defect？

The-Defect 是一款基于 **ESP32-P4** 的多人 AI 游戏主机，搭配 **ESP32-C6** 无线手柄。它拥有 6 寸 720×1280 MIPI DSI 触摸屏、BLE 无线手柄、AI 助手、游戏市场、Lua 脚本引擎等丰富功能。

> **一句话总结**：一台可以自己生成游戏、联机对战、联网下载的掌上游戏机。

## 核心特性

| 特性 | 说明 |
|------|------|
| 🖥️ 高清触屏 | 6 寸 720×1280 MIPI DSI 屏幕，GT911 五点触控 |
| 🎮 无线手柄 | ESP32-C6 BLE HID 手柄，支持最多 4 个 |
| 🤖 AI 助手 | 内置 OpenAI 兼容 API，可对话、调系统、生成 Lua 游戏 |
| 📦 ELF 插件系统 | 游戏编译为 .so 插件，运行时动态加载，热更新无需重烧固件 |
| 🐍 Lua 引擎 | 内置 Lua 5.5 沙箱，可运行 AI 生成或市场下载的 Lua 游戏 |
| 🏪 游戏市场 | 从社区服务器浏览、下载、更新 Lua/ELF 游戏 |
| 🌐 多人联机 | 支持 P4→P4 UDP 联机 + WebSocket 远程客户端 |
| 📡 屏幕串流 | MJPEG 硬件编码 + WebSocket 实时流，浏览器可查看屏幕 |
| 🔧 OTA 升级 | 双 OTA 槽位空中升级，bootloader 自动回滚保护 |
| 🔋 电池管理 | ADC 电量检测 + 低电量提醒 + 充电动画 |
| 📝 日志系统 | 日志重定向到 FAT 文件 + WebSocket 实时推送，封壳设备远程调试 |
| 🕐 时间同步 | SNTP 自动时间同步，NVS 持久化 |

## 硬件规格一览

| 组件 | 型号/规格 |
|------|-----------|
| 主控 | ESP32-P4（双核 RISC-V 360MHz） |
| 协处理器 | ESP32-C6（WiFi + BLE） |
| 屏幕 | 6 寸 MIPI DSI，ILI9881C 驱动，720×1280 |
| 触摸 | GT911（I²C），最多 5 点触控 |
| 音频 | ES8311 编解码器 + I²S + 功放 |
| 存储 | 32MB Flash（6MB×2 OTA + 16MB FAT）+ Octal PSRAM |
| 手柄 | ESP32-C6 BLE HID，最多 4 个 |
| 电池 | ADC1_CH6 分压检测，GPIO23 充电检测 |

## 软件架构速览

```
┌─────────────────────────────────────────┐
│              桌面 & 系统 App              │
│   DesktopApp / Settings / Market / ...   │
├─────────────────────────────────────────┤
│         ELF 插件层 (.so 游戏)            │
│   snake / tetris / fruitNinja / ...      │
├─────────────────────────────────────────┤
│         Lua 引擎 (沙箱化)                │
│   AI 生成游戏 / 市场 Lua 脚本            │
├─────────────────────────────────────────┤
│         AI Agent                         │
│   OpenAI API / 动作执行 / 多轮对话        │
├─────────────────────────────────────────┤
│         LVGL 9.5 UI 框架                 │
│   ILI9881C / GT911 / FreeType 字体       │
├─────────────────────────────────────────┤
│         基础设施                          │
│   Task / Mutex / GPIO / I²C / WiFi / ... │
├─────────────────────────────────────────┤
│         ESP-IDF v5.5.2 + FreeRTOS        │
│         ESP32-P4 + ESP32-C6              │
└─────────────────────────────────────────┘
```

## 文档导航

| 文档 | 适合谁 | 内容 |
|------|--------|------|
| [02-硬件架构](./02-hardware.md) | 想了解硬件的人 | 引脚配置、外设连接、电气特性 |
| [03-软件架构](./03-software-architecture.md) | 想了解系统的人 | 模块划分、启动流程、数据流 |
| [04-编译烧录](./04-build-and-flash.md) | 开发者 | 环境搭建、编译、烧录、OTA |
| [05-ELF插件开发](./05-elf-plugin-dev.md) | 游戏开发者 | 创建 ELF 插件游戏 |
| [06-Lua游戏开发](./06-lua-game-dev.md) | 游戏创作者 | 用 Lua 编写游戏 |
| [07-网络与联机](./07-networking.md) | 想了解联机的人 | WiFi、多主机、WebSocket |
| [08-市场系统](./08-market-system.md) | 游戏发布者 | 市场服务器与客户端 |
| [09-AI助手](./09-ai-assistant.md) | 想了解 AI 的人 | AI Agent 架构与能力 |
| [dev/01-代码约定](../dev/01-code-conventions.md) | 贡献者 | 代码风格、命名规范 |
| [dev/02-模块索引](../dev/02-module-index.md) | 开发者 | 所有模块路径与职责 |
| [dev/03-已知坑与调试](../dev/03-known-pitfalls.md) | 踩坑的人 | P4 常见问题与解决方案 |

## 快速开始

1. **克隆仓库**（含子模块）：
   ```bash
   git clone --recurse-submodules <仓库地址>
   ```

2. **搭建 ESP-IDF v5.5.2 环境**：
   ```bash
   . $IDF_PATH/export.sh
   ```

3. **编译**：使用 ESP-IDF 扩展的 `Build` 命令，或：
   ```bash
   idf.py build
   ```

4. **烧录**（首次需要 USB）：
   ```bash
   idf.py -p /dev/ttyACMx flash
   ```

5. **后续升级**：封壳设备通过 OTA，浏览器访问 `http://<主机名>.local/server/ota/`

## 许可证

本项目使用 [LICENSE](../LICENSE) 中定义的许可协议。
