# The-Defect — ESP32-P4 多人游戏主机

ESP32-P4 驱动的多人游戏主机 + ESP32-C6 无线手柄。面向局域网派对游戏场景。

> 📡 **手柄固件**：[esp32c6HidJoystick](https://codeberg.org/loouwiit/esp32c6HidJoystick) — 自研 ESP32-C6 BLE HID 手柄，与本机配对使用。

---

## 特色亮点

### 🎮 多人派对游戏

| 特色 | 说明 |
|------|------|
| **4 人 BLE 手柄同时连接** | 自研 ESP32-C6 无线手柄，NimBLE 协议栈，即连即玩 |
| **主机-房间架构** | 游戏 App 自带房间系统，自动创建/加入 |
| **AI 对手** | 中国象棋内置 AI 引擎，单人也能玩 |

### 🖥️ 主机 + 协处理器分离架构

```
ESP32-P4 (主机)  — 6寸 720×1280 触屏 + LVGL GUI + 游戏逻辑
      ↕ SDIO (esp_hosted)
ESP32-C6 (协处理器) — WiFi STA/AP 双模 + BLE 蓝牙射频
```

P4 专注渲染和游戏计算，C6 处理无线通信，双芯并行互不抢占。

### 🔌 即开即玩，零配置

1. 开机自动尝试连接上次保存的 WiFi
2. 连接失败 → 自动切换到 **AP 热点模式**（SSID: `esp32p4`）
3. 手机/笔记本连接热点 → 浏览器访问 `esp32p4.local` 投屏+触控
4. BLE 手柄开机 → 自动重连已配对设备，5 秒内恢复

全过程无需路由器，无需下载 App，不需要任何配置。

### 🔧 全链路自研，100% 开源

| 层次 | 技术 |
|------|------|
| 硬件 | ESP32-P4 + ESP32-C6，自研 PCB |
| 固件 | ESP-IDF 5.x + C++20 |
| GUI | LVGL 9.4 + PPA 硬件加速渲染 |
| 通信 | 自研 HTTP/WebSocket 服务器，裸 socket，无第三方依赖 |
| 手柄 | 自研 BLE HID Gamepad 固件 + NimBLE 协议栈 |
| 音频 | ES8311 编解码器 + esp_audio_render 多流混音 |

---

## 硬件架构

```
ESP32-P4 (主机)
├── MIPI DSI — ILI9881C 6寸 720×1280 屏幕
├── I²C      — GT911 触摸 (GPIO7/8)
├── SDIO     — ESP32-C6 (esp-hosted, WiFi + BLE 协处理器)
├── I²S      — ES8311 音频编解码器 + 扬声器
├── ADC      — 电池电量检测 (ADC1_CH6)
├── GPIO23   — 充电检测 (低电平=充电中)
└── GPIO     — 功放使能 (GPIO53)

ESP32-C6 (协处理器)
├── SDIO     — ESP32-P4 通信 (esp_hosted)
├── BLE      — 手柄连接 (NimBLE controller)
├── WiFi     — STA/AP 双模 (通过 esp_hosted 转发)
└── 2.4GHz   — 蓝牙 + WiFi 共用天线
```

---

## 功能

### 游戏
| 游戏 | 联机 | 说明 |
|------|------|------|------|
| 🧱 **俄罗斯方块** | ✅ 3 人对战 | 现代俄罗斯方块，7-bag，SRS |
| 🍉 **水果忍者** | ✅ 2 人对战 | 多人同屏切水果 |
| 🐍 **贪吃蛇** | ✅ 4 人对战 | 经典蛇对战 |
| ♟️ **中国象棋** | ✅ 双人 + AI | 本地双人 / 内置 AI 引擎 |
| 🎮 **桌面启动器** | — | 5 游戏卡片网格，3 焦点组导航 |

所有界面均支持触摸输入与蓝牙手柄输入

### 设置
- **BLE 手柄管理** — 扫描、配对、断开、NVS 持久化
- **WiFi 配置** — AP 扫描、密码输入、连接状态
- **时间设置** — 手动调时 / SNTP 自动同步
- **电源管理** — 电量显示、关机 Deep-sleep

### 系统
- **屏幕串流** — MJPEG HTTP 实时投屏（硬件 JPEG 编码，零拷贝）
- **Web 触控注入** — 通过浏览器远程触摸操作
- **音频播放** — 多流混音，MP3/AAC/FLAC/WAV 解码
- **CPU 监控** — 串口实时任务利用率采样
- **mDNS** — `esp32p4.local` 自动发现
- **WiFi AP** — 断开时自动切换到热点模式

---

## 软件架构

```
┌──────────────────────────────────────────────────────────┐
│                     App 层 (12 Apps)                      │
│  DesktopApp │ Tetris │ FruitNinja │ Snake │ ChineseChess  │
│  BleSettings │ WifiSettings │ TimeSettings │ PowerMgmt    │
├───────────────────────┬──────────────────────────────────┤
│     AppStackManager   │         网络服务                  │
│  ├─ Stack 0 (桌面)    │  ├─ HTTP Server (port 80)        │
│  ├─ Stack 1..N (游戏) │  ├─ WS Server  (port 8080)       │
│  └─ 异步删除 + 去重   │  ├─ ScreenStream (MJPEG)         │
│                       │  └─ mDNS (_http, _ws)            │
├──────────┬────────────┴────────────┬─────────────────────┤
│ 显示/输入 │     WiFi / BLE         │      存储            │
│ Display  │  ├─ STA/AP 双模 + NAT  │ FAT (16MB)          │
│ ILI9881C │  ├─ C6 Slave (SDIO)    │ memFS (6MB)         │
│ GT911    │  ├─ BLE Gamepad (4P)   │ SD Card             │
│ FontLoader│ └─ mDNS               │                     │
├──────────┴─────────────────────────┴─────────────────────┤
│                   基础设施层                               │
│  Task(协程) │ Mutex(RAII) │ GPIO │ IIC │ GUI(色板+工厂)  │
│  CPU Monitor │ Battery(ADC) │ Audio(ES8311+AudioHandle) │
└──────────────────────────────────────────────────────────┘
```

### 技术栈

| 技术 | 用途 |
|------|------|
| ESP32-P4 | 主 SoC（双核 HP + LP Core） |
| ESP32-C6 | 协处理器（WiFi/BLE） |
| ESP-IDF 5.x | 固件框架 |
| LVGL 9.4 | GUI 框架 |
| ILI9881C | 6" 720×1280 MIPI DSI 面板 |
| GT911 | 5 点电容触摸 |
| ES8311 | 音频编解码器 |
| NimBLE | BLE 协议栈（P4 host + C6 controller） |
| esp_hosted | P4↔C6 SDIO 通信 |
| FreeType | 字体渲染（NotoSC） |
| esp_audio_render | 多流混音音频渲染 |
| PPA | 2D 硬件加速（fill/blend/SRM） |
| 硬件 JPEG | ESP32-P4 内置 JPEG 编码器 |

### 模块一览

详见 `AGENTS.md` 的完整架构文档。核心模块摘要：

| 层次 | 模块 | 路径 |
|------|------|------|
| 基础设施 | Task / Mutex / GPIO / IIC | `main/task/` `main/mutex/` `main/gpio/` `main/iic/` |
| 显示/输入 | Display / ILI9881c / FontLoader / Touch / VirtualIndev | `main/display/` `main/touch/` `main/virtualIndev/` |
| 导航 | App / AppStack / AppStackManager | `main/app/app*.hpp` |
| 应用 | 12 个 App | `main/app/*/` |
| 网络 | WiFi / C6 Slave / SocketStream / HTTP Server / WS Server / mDNS | `main/wifi/` `main/server/` `main/wsServer/` |
| 媒体 | ScreenStream / ES8311 / Audio | `main/screenStream/` `main/audio/` |
| 外设 | BLE Gamepad / Battery / CPU Monitor | `main/bleGamepad/` `main/battery/` `main/monitor/` |
| GUI | 色板 + 工厂方法 | `main/gui/` |
| 存储 | FAT / memFS / SD | `main/storage/` |

---

## 构建与烧录

### 前提

- ESP-IDF 5.x（含 esp32p4 和 esp32c6 支持）
- ESP-IDF 扩展（VS Code）

### 构建

```bash
idf.py build
```

### 补丁

项目依赖 3 个对 managed_components 的补丁：

```bash
bash patches/apply.sh
```

> ⚠ `idf.py reconfigure` 后需重新打补丁。

### 烧录

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

### 分区表

| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 16KB | 配置持久化 |
| otadata | 8KB | OTA 数据 |
| phy_init | 4KB | 射频校准 |
| ota_0 | 6MB | 应用固件 |
| fat | 16MB | 数据分区 (/root/) |

### 资源上传

`reserces/` 目录需上传到 FAT 分区：

```
reserces/
├── server/       ← HTTP 网页资源
└── system/       ← 系统文件 (字体等)
```

---

## 网络服务

| 服务 | 端口 | 协议 | 说明 |
|------|------|------|------|
| HTTP | 80 | TCP | 网页服务 + 屏幕串流 |
| WebSocket | 8080 | TCP | 游戏通信 + 触控注入 |
| mDNS | 5353 | UDP | 服务发现 (`esp32p4.local`) |

---

## 项目结构

```
├── main/
│   ├── main.cpp              ← 启动入口
│   ├── app/                  ← 应用层 (12 Apps)
│   ├── audio/                ← ES8311 + Audio 管理器
│   ├── battery/              ← 电池管理
│   ├── bleGamepad/           ← BLE 手柄 (NimBLE + esp_hosted)
│   ├── display/              ← LVGL 适配 + ILI9881c + 字体
│   ├── gpio/                 ← GPIO 封装
│   ├── gui/                  ← GUI 辅助类
│   ├── iic/                  ← I²C 总线
│   ├── monitor/              ← CPU 监视器
│   ├── mutex/                ← RAII 互斥锁
│   ├── screenStream/         ← 屏幕串流
│   ├── server/               ← HTTP 服务器
│   ├── storage/              ← FAT / memFS / SD
│   ├── task/                 ← 协程调度器
│   ├── touch/                ← GT911 触摸
│   ├── virtualIndev/         ← 虚拟触摸注入
│   ├── wifi/                 ← WiFi / mDNS / TCP Socket
│   └── wsServer/             ← WebSocket 服务器
├── gamepad/                  ← 手柄固件 (git submodule, esp32c6 target)
├── patches/                  ← managed_components 补丁
├── reserces/                 ← FAT 分区资源文件
│   ├── server/               ← HTTP 静态资源
│   └── system/               ← 系统文件
├── AGENTS.md                 ← 完整架构文档 (AI Agent 指南)
└── partitions.csv            ← 分区表

---

## 相关项目

| 项目 | 仓库 | 路径 | 说明 |
|------|------|------|------|
| **esp32c6HidJoystick** | [codeberg.org/loouwiit/esp32c6HidJoystick](https://codeberg.org/loouwiit/esp32c6HidJoystick) | `gamepad/` (submodule) | ESP32-C6 BLE HID 无线手柄固件，与本机通过 NimBLE 配对通信 |

### 克隆完整项目

```bash
git clone --recurse-submodules https://<本仓库地址>
# 或如果已克隆但未拉子模块：
git submodule update --init --recursive
```

手柄固件位于 `gamepad/` 目录，独立构建（ESP-IDF + esp32c6 target）。
