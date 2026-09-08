# The-Defect 文档中心

## 📚 Wiki（新手入门）

面向新用户和初次接触项目的人。

| 文档 | 内容 |
|------|------|
| [01-项目概述](wiki/01-project-overview.md) | 项目介绍、核心特性、硬件规格、快速上手 |
| [02-硬件架构](wiki/02-hardware.md) | 系统框图、引脚分配、Flash 分区、PSRAM 配置 |
| [03-软件架构](wiki/03-software-architecture.md) | 技术栈、模块划分、启动流程、线程模型 |
| [04-编译烧录](wiki/04-build-and-flash.md) | 环境搭建、编译、USB 烧录、OTA 升级、远程调试 |
| [05-ELF插件开发](wiki/05-elf-plugin-dev.md) | 创建 ELF 游戏插件、构建上传、可用 API |
| [06-Lua游戏开发](wiki/06-lua-game-dev.md) | Lua 游戏契约、可用模块、完整示例 |
| [07-网络与联机](wiki/07-networking.md) | WiFi 模式、mDNS、多主机联机、WebSocket/HTTP 服务 |
| [08-市场系统](wiki/08-market-system.md) | 游戏市场架构、客户端/服务器协议、发布流程 |
| [09-AI助手](wiki/09-ai-assistant.md) | AI Agent 架构、系统动作、多轮对话、输入法 |

## 🔧 Developer（开发者）

面向项目贡献者和核心开发者。

| 文档 | 内容 |
|------|------|
| [01-代码约定](dev/01-code-conventions.md) | 命名规范、日志规范、错误处理、并发模型 |
| [02-模块索引](dev/02-module-index.md) | 完整源码目录结构、模块依赖关系 |
| [03-已知坑与调试](dev/03-known-pitfalls.md) | P4 内部 RAM、LVGL 锁、ELF loader、调试技巧 |

## 📋 其他文档

| 文档 | 内容 |
|------|------|
| [elf-plan-archive.md](elf-plan-archive.md) | ELF 插件化完整历史存档（设计决策、ABI 结论、测试记录） |
| [../../AGENTS.md](../AGENTS.md) | AI 协作指南（发布版） |
| [../../newAGENTS.md](../newAGENTS.md) | AI 协作指南（草稿版） |
| [../../readme.md](../readme.md) | 项目 README |
