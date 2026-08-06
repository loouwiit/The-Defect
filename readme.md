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
| **5 点多指同屏触控** | GT911 硬件 5 点 → 五 indev 多指桥接（`GT911Indev`），多指独立 LVGL 事件，同屏多人直接触屏玩（双指切水果、各自 D-pad 等） |

### 🤖 AI Agent — 自然语言操控设备

集成 OpenAI 兼容 API，用户通过触屏或手柄输入自然语言，AI 自主规划并执行操作：

- **系统层浮动窗**：驻留 `lv_layer_sys`，不占用 App 栈，底层 App 不受影响
- **多种唤出方式**：顶部下滑 / 手柄组合键(Y+X+B)
- **可拖拽窗口**：浮动窗可任意拖动位置，附带 LVGL 虚拟键盘（拼音/五笔/混合中文输入，空格上屏候选）
- **13 个系统操作**：开关应用、焦点导航(select/activate)、调亮度/音量、WiFi 连接、设备信息、关机等
- **焦点系统集成**：所有 App 接入 `onAiSelect()` / `onAiActivate()`，AI 可通过焦点组导航操作任意界面
- **多轮协作**：AI 通过 `nextRound` 自主控制多轮交互，跨 App 探索（如打开时间设置 → 同步时间）
- **长期记忆**：AI 通过 `memory` 字段自行维护跨对话的用户偏好
- **对话历史**：保留最近 5 轮对话，超出的由 AI 自主写入长期记忆
- **App 自定义动作**：各 App 通过 `getAiContext()` / `executeAiAction()` 暴露专属操作
- **动态 Lua 游戏生成**：AI 能生成完整的 Lua 游戏脚本并自动运行（详见下方）
- **Lua 脚本管理器**：独立 App 提供脚本列表浏览、运行、删除、重命名、AI 生成入口
- **自我修正**：游戏运行出错时自动回传错误给 AI，AI 可修复代码后重新生成
- **多模型兼容**：OpenAI / Ollama / DeepSeek 等，配置即用

```
用户输入 + 5轮历史 + 长期记忆
  ↓
① LLM 推理 → 返回 JSON actions
  ↓
② AiExecutor 顺序执行
  ├─ system actions（亮度、导航、select/activate 等）
  └─ App::onAiSelect/onAiActivate（焦点操控）
  ↓
③ 如需继续（nextRound=true）→ 重刷 {app_context} → 回到 ①
  ↓ (最多 5 轮)
④ 更新 memory + 对话历史
```

### � AI 动态生成 Lua 游戏

集成 Lua 5.5 脚本引擎，AI 可以通过 AI Agent 界面动态生成并运行游戏：

- **Lua 游戏脚本**：AI 生成完整 Lua 代码 → 保存到 `/root/lua/game/test.lua` → `LuaGameApp` 自动运行
- **简化绘图 API**：`display.fill_rect()` / `draw_text()` / `draw_circle()` 等，易于 AI 理解和生成
- **C++ 驱动循环**：独立 Task 以 60fps 固定帧率调用 Lua `loop(dt)` 函数
- **手柄输入**：Lua 通过 `input.is_pressed()` / `input.joystick()` 轮询读取手柄状态
- **错误回传**：运行时错误自动记录，用户下次对话时注入 AI 上下文，促成 AI 自我修复
- **读取已生成代码**：AI 可通过 `read` 动作读取当前游戏脚本内容，了解状态后精准迭代
- **Lua 脚本管理器**：`LuaManagerApp` 独立 App，桌面第六张卡片进入。支持脚本列表浏览(按目录分组)、运行、删除(含确认弹框)、重命名(LVGL 键盘)、AI 生成新游戏入口

```
用户说"写一个贪吃蛇游戏"
  ↓
① AI 生成 Lua 代码 + generate_lua_game 动作
  ↓
② LuaGameApp 保存到 FAT 并启动 60fps 循环
  ↓
③ 出错时 → 报错给 AI → 下次对话 AI 修复 → 重新生成
```

### 🌐 游戏市场 — 社区 Lua 游戏下载

集成网络市场，浏览并下载其他作者发布的 Lua 游戏脚本，一键运行：

- **独立 App**：桌面第七张卡片「游戏市场」进入
- **截图卡片网格**：一级界面显示横屏截图缩略图 + 名称 + 作者 + 版本，缩略图懒加载
- **详情页**：大图 / 简介 / 版本 / 更新时间 / 下载量 / 评分 + 下载运行
- **已下载识别**：下载存 `/root/lua/market/`，已下载脚本显示「✓ 运行」直接本地启动（离线可玩），卡片带「已装」角标
- **下载量上报**：运行统计自动上报服务端
- **服务端独立仓库**：`The-Defect-Market`（git submodule），静态内容走 `market/<作者>/<游戏>/` 文件系统（作者 PR 即上架），动态数据（评分/下载量/搜索）走 SQLite，提供 `/api/search` 搜索接口

### �🌐 多主机跨局域联结机

- **BLE 配对风格连接**：ConsoleManager 扫描局域网内主机，点击即成为从机
- **1 主机 + ≤3 从机**：4 台 P4 设备互联，共享手柄输入
- **标准化输入转发**：系统层 VirtualGamepad 统一管理，mask 掩码控制输入路由
- **主机权威 mask**：主机决定各从机哪些手柄进入本地游戏，从机自动同步反码
- **UDP 零拷贝转发**：从机→主机 11 字节 UDP 帧，持久 socket，60fps
- **WebSocket 管理通道**：`/ws/console` 管理注册/心跳/全量 slots_update 广播
- **槽位交换**：ConsoleManager 支持拖拽式交换，mask 随 slot 同步迁移

**Tetris 跨主机远程游玩**：
- **主机连接从机**：主机 TetrisApp 通过 TCP 主动连接从机的 `TetrisRemoteApp`（端口 8084），避免时序问题
- **异步 snapshot 发送**：游戏循环通过 `memcpy` 推送快照，专用 Send Task（20fps）异步序列化 JSON + TCP send，不阻塞游戏 60fps 帧率
- **从机渲染**：`TetrisRemoteApp` 全屏渲染远程主机画面，支持 1-3 玩家动态分屏
- **输入零配置**：从机 BLE 手柄输入经由 UDP 8082 + `VirtualGamepad` 自动转发到主机，`TetrisRemoteApp` 无需手动转发

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
| 🎮 **桌面启动器** | — | 7 游戏卡片（含 Lua 脚本管理 + 游戏市场），3 焦点组导航 |

所有界面均支持多指触摸输入与蓝牙手柄输入

### 设置
- **游戏市场** — 浏览并下载社区 Lua 游戏（截图网格 + 详情 + 下载运行/本地运行），桌面第七张卡片进入
- **Lua 脚本管理** — 浏览/运行/删除/重命名 Lua 脚本，AI 生成新游戏入口
- **BLE 手柄管理** — 扫描、配对、断开、NVS 持久化
- **WiFi 配置** — AP 扫描、密码输入、连接状态
- **多主机管理** — BLE 配对风格界面，mDNS 发现 + 4 槽位 + 掩码控制 + 交换
- **时间设置** — 手动调时 / SNTP 自动同步（默认开启：连接 WiFi 即同步，每小时自动校正）
- **电源管理** — 电量显示、关机 Deep-sleep

### 系统
- **屏幕串流** — MJPEG HTTP 实时投屏（硬件 JPEG 编码，零拷贝）
- **Web 触控注入** — 通过浏览器远程触摸操作
- **音频播放** — 多流混音，MP3/AAC/FLAC/WAV 解码
- **CPU 监控** — 串口实时任务利用率采样
- **mDNS** — `esp32p4.local` 自动发现，支持自定义主机名（NVS 持久化，运行时热重启）
- **设备发现** — 通过 `_p4console._tcp` 服务类型枚举局域网内其他游戏主机
- **WiFi AP** — 断开时自动切换到热点模式
- **输入法** — 拼音/五笔/混合中文输入，自定义词库（Rime 提取、FAT 加载、五笔优先拼音兜底）、空格上屏候选
- **时间同步** — SNTP 自动同步，默认开启 + NVS 持久化，WiFi 连接即同步、每小时自动校正

---

## 软件架构

```
┌──────────────────────────────────────────────────────────┐
│                     App 层                                │
│  DesktopApp │ Tetris │ FruitNinja │ Snake │ ChineseChess  │
│  BleSettings │ WifiSettings │ TimeSettings │ PowerMgmt   |
│  ConsoleManagerApp   │   AiAgent     |  testApp           |
├───────────┬─────────────────────────┬───────────────────────────────┤
│  AppStackManager     │  VirtualGamepad                  │
│  ├─ Stack 0 (桌面)    │  ├─ inject (BLE/远程/WS/UDP)     │
│  ├─ Stack 1..N (游戏) │  ├─ Dispatch Task @60fps        │
│  └─ 异步删除 + 去重   │  ├─ hostMask 掩码过滤 + 广播    │
│                      │  └─ Forwarder Thread (UDP 从机)  │
│  ConsoleManagerApp   ├─────────────────────────────────┤
│  ├─ mDNS 扫描         │  ├─ HostManager                 │
│  ├─ 4 槽位 + 交换     │  │  ├─ 角色 (主机/从机)         │
│  ├─ mask 控制 (P0-P3) │  │  ├─ 槽位管理 + mask 下发     │
│  └─ 主机/从机双视图   │  │  └─ WsClient 连接池          │
│                      │  ├─ wsConsoleHandler            │
│                      │  │  └─ /ws/console slots_update │
│                      │  ├─ HTTP Server (port 80)        │
│                      │  ├─ WS Server  (port 8080)       │
│                      │  │  ├─ /ws/touch  触控注入       │
│                      │  │  ├─ /ws/input  手柄透传(Web)  │
│                      │  │  ├─ /ws/stream 屏幕串流       │
│                      │  │  └─ /ws/console 设备管理      │
│                      │  ├─ UDP Server (port 8082)       │
│                      │  │  └─ P4→P4 低延迟手柄转发      │
│                      │  ├─ ScreenStream (MJPEG)         │
│                      │  ├─ mDNS (_http, _ws, _p4console)│
│                      │  └─ Discovery (设备枚举)         │
├──────────┬────────────┴────────────┬─────────────────────┤
│ 显示/输入 │     WiFi / BLE         │      存储            │
│ Display  │  ├─ STA/AP 双模 + NAT  │ FAT (16MB)          │
│ ILI9881C │  ├─ C6 Slave (SDIO)    │ memFS (6MB)         │
│ GT911    │  ├─ BLE Gamepad (4P)   │ SD Card             │
│ GT911Indev│ └─ mDNS              │                     │
│ FontLoader│                      │                     │
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
| GT911 | 5 点电容触摸（五 indev 多指桥接，多指独立 LVGL 事件） |
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
| AI Agent | AiClient / AiParser / AiExecutor | `main/aiAgent/` |
| 基础设施 | Task / Mutex / GPIO / IIC / VirtualGamepad | `main/task/` `main/mutex/` `main/gpio/` `main/iic/` `main/gamepad/` |
| 跨主机联机 | HostManager / wsConsoleHandler / wsInputHandler / udpInput / WsClient / ConsoleManagerApp / **tetrisTcpHost** / **TetrisRemoteClient** / **TetrisRemoteApp** | `main/host/` `main/websocket/` `main/app/consoleManager/` `main/app/tetris/net/` `main/app/tetris/` |
| 显示/输入 | Display / ILI9881c / FontLoader / Touch / **GT911Indev** / VirtualIndev | `main/display/` `main/touch/` `main/virtualIndev/` |
| 导航 | App / AppStack / AppStackManager | `main/app/app*.hpp` |
| 应用 | 12 个 App（+AI 助手） | `main/app/*/` |
| 网络 | WiFi / C6 Slave / SocketStream / HTTP Server / WS Server / mDNS / Discovery | `main/wifi/` `main/server/` `main/wsServer/` `main/discovery/` |
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

项目依赖 4 个对 managed_components 的补丁：

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
└── system/       ← 系统文件（字体 + 输入法词库）
    ├── NotoSC.ttf
    ├── pinyin_dict.txt       ← 拼音词库
    ├── wubi_dict.txt         ← 五笔词库
    └── wubi_pinyin_dict.txt  ← 五笔+拼音混合词库（五笔优先）
```

---

## 网络服务

| 服务 | 端口 | 协议 | 说明 |
|------|------|------|------|
| HTTP | 80 | TCP | 网页服务 + 屏幕串流 |
| WebSocket | 8080 | TCP | 触控注入 + 游戏通信 (Web) + 设备管理 |
| UDP | 8082 | UDP | P4→P4 低延迟手柄输入转发 |
| **Tetris 游戏数据** | **8083** | **TCP** | **主机 TCP 服务端（接收旧式 WS 兼容连接）** |
| **Tetris 远程游玩** | **8084** | **TCP** | **从机监听，主机主动连接（反客为主模式）** |
| mDNS | 5353 | UDP | 服务发现 (`esp32p4.local`) |

---

## 项目结构

```
├── main/
│   ├── main.cpp              ← 启动入口
│   ├── app/                  ← 应用层 (15 Apps)
│   │   ├── aiAgentApp/       ← AI 助手 + PinyinDict 输入法词库
│   │   ├── luaGameApp/       ← Lua 游戏运行器
│   │   ├── luaManagerApp/    ← Lua 脚本管理器
│   │   └── marketApp/        ← 游戏市场
│   ├── audio/                ← ES8311 + Audio 管理器
│   ├── battery/              ← 电池管理
│   ├── bleGamepad/           ← BLE 手柄 (NimBLE + esp_hosted)
│   ├── display/              ← LVGL 适配 + ILI9881c + 字体
│   ├── gpio/                 ← GPIO 封装
│   ├── gui/                  ← GUI 辅助类
│   ├── host/                 ← 跨主机联机 (HostManager / wsInputHandler / wsConsoleHandler / udpInput)
│   ├── iic/                  ← I²C 总线
│   ├── lua/                  ← Lua 5.5 引擎（沙箱）+ 绑定模块
│   ├── market/               ← 游戏市场 HTTP 客户端
│   ├── monitor/              ← CPU 监视器
│   ├── mutex/                ← RAII 互斥锁
│   ├── screenStream/         ← 屏幕串流
│   ├── server/               ← HTTP 服务器
│   ├── storage/              ← FAT / memFS / SD
│   ├── task/                 ← 协程调度器
│   ├── touch/                ← GT911 触摸
│   ├── virtualIndev/         ← 虚拟触摸注入
│   ├── websocket/            ← WsClient 通用 WebSocket 客户端
│   ├── wifi/                 ← WiFi / mDNS / TCP Socket
│   └── wsServer/             ← WebSocket 服务器
├── gamepad/                  ← 手柄固件 (git submodule, esp32c6 target)
├── The-Defect-Market/        ← 游戏市场服务器 + 社区游戏内容 (git submodule)
├── patches/                  ← managed_components 补丁（6 个）
├── reserces/                 ← FAT 分区资源文件
│   ├── lua/                  ← Lua 脚本
│   ├── server/               ← HTTP 静态资源
│   └── system/               ← 系统文件（字体 + 输入法词库）
├── tools/                    ← 词库生成脚本（gen_pinyin_dict.py / gen_wubi_dict.py / gen_wubi_pinyin_dict.py）
├── AGENTS.md                 ← 完整架构文档 (AI Agent 指南)
└── partitions.csv            ← 分区表

---

## 相关项目

| 项目 | 仓库 | 路径 | 说明 |
|------|------|------|------|
| **esp32c6HidJoystick** | [codeberg.org/loouwiit/esp32c6HidJoystick](https://codeberg.org/loouwiit/esp32c6HidJoystick) | `gamepad/` (submodule) | ESP32-C6 BLE HID 无线手柄固件，与本机通过 NimBLE 配对通信 |
| **The-Defect-Market** | [codeberg.org/loouwiit/The-Defect-Market](https://codeberg.org/loouwiit/The-Defect-Market) | `The-Defect-Market/` (submodule) | 游戏市场服务器 + 社区 Lua 游戏内容仓库（文件系统存静态、SQLite 存动态），作者 PR 游戏即上架 |

### 克隆完整项目

```bash
git clone --recurse-submodules https://<本仓库地址>
# 或如果已克隆但未拉子模块：
git submodule update --init --recursive
```

手柄固件位于 `gamepad/` 目录，独立构建（ESP-IDF + esp32c6 target）。
游戏市场服务器位于 `The-Defect-Market/` 目录，独立部署：`cd The-Defect-Market && node server.js`。
