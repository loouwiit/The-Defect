# Plan: Multiplayer Tetris App

## 概述

在 ESP32-P4 上实现一个 **Guideline-compliant 现代俄罗斯方块**多人游戏，支持 1-3 人同屏对战（等宽竖分）。玩家使用 ESP32-C6 无线手柄或触屏操作，多台主机通过 WebSocket 互联传递垃圾行。

**核心规则**:
- 棋盘: 10×20（可视），隐藏区 22 行高
- SRS (Super Rotation System) + Wall Kick
- 7-bag Randomizer（全局共享，公平出块）
- Hold 功能（每块一次）
- Ghost Piece
- Lock Delay (1000ms，移动/旋转重置)
- DAS/ARR 移动
- 消行计分 & Back-to-Back T-Spin
- T-Spin 判定：三角判定 + 旋转进入（横移不算）
- 垃圾行机制（Battle Royale 核心）
- Combo 攻击加成（连续消行 +combo 攻击行）

**多人模式**: Battle Royale — 每人独立棋盘，消行产生垃圾行发送给其他玩家，最后存活者胜。

**垃圾行目标策略**（初期）：固定攻击对面玩家。后续可演进为攻击当前分数最高者。

## 架构（三层分离）

```
┌─────────────────────────────────────────────────────────┐
│  UI 层（TetrisRenderer）                                  │
│  - 只负责渲染棋盘/方块/分数/UI                            │
│  - 不包含任何游戏逻辑                                    │
│  - 与 GameLogic 层通过数据接口通信                       │
├─────────────────────────────────────────────────────────┤
│  游戏逻辑层（TetrisClient / TetrisServer）               │
│  - TetrisClient: 方块移动/碰撞/消行/计分                 │
│  - TetrisServer: 7-bag生成 + 消息路由（仅 Host 运行）    │
│  - 完全不知道谁在看，只提供数据接口                      │
├─────────────────────────────────────────────────────────┤
│  网络层（NetManager）                                    │
│  - WebSocket 消息收发                                    │
│  - 连接管理                                              │
└─────────────────────────────────────────────────────────┘
```

**好处**：
- 同一套游戏逻辑，套上不同 UI（正常视图 / Zoom 视图）
- Server/Client 测试不需要 UI（纯数据接口）
- 后续支持 Bot 只需注入 Client（通过 IInputSource）

## 文件结构

```
main/app/tetris/
├── tetrisRoomApp.hpp/cpp     ← 房间菜单（创建/加入/等待）
├── tetrisApp.hpp/cpp         ← 主游戏（分屏 + Zoom 模式）
├── gameLogic/
│   ├── tetris_client.hpp/cpp    ← 纯游戏逻辑（Board/Piece/Queue/Hold/Scoring）
│   └── tetris_server.hpp/cpp    ← 7-bag + 消息路由（仅 Host）
├── renderer/
│   └── tetris_renderer.hpp/cpp ← LVGL 渲染（与 gameLogic 解耦）
├── net/
│   └── tetris_net.hpp/cpp      ← WebSocket 消息处理
└── input/
    └── tetris_input.hpp/cpp    ← 输入抽象层
```

## 状态同步方案（最终版）

**Host 统一发 Piece + 本地游戏逻辑 + 透传棋盘/Move**：

```
┌─────────────────────────────────────────────────────────────┐
│  Host (Player 0) — 内置 Server                              │
│  ├── 维护唯一 7-bag                                          │
│  ├── 接收 lock 通知 → 广播 piece                            │
│  ├── 透传 board / move / join 消息                        │
│  └── 路由 garbage（attack → 目标玩家）                     │
│                                                              │
│  Client (Player 1/2/3)                                      │
│  ├── 本地运行完整游戏逻辑                                    │
│  ├── 锁块 → 发给 Host                                       │
│  ├── 接收 piece → 继续本地逻辑                              │
│  ├── 发送 board_delta/full / move 给所有人（经 Host 透传） │
│  └── 接收 garbage → 本地 addGarbage                         │
└─────────────────────────────────────────────────────────────┘
```

**本地联机 = 内置 Server + 2 个 Client**，和 Minecraft 局域网模式完全一致。

### 为什么 Move 不需要消息？

方块移动/旋转只影响**本地消行**，不影响任何人的游戏逻辑。因此：
- 移动/旋转：本地处理，0 消息（本地分屏玩家直接看到）
- 攻击（attack）：唯一跨玩家消息
- board_delta/full / move：观众预览用的棋盘同步，不参与游戏逻辑

### 消息集

| 消息 | 方向 | 内容 | 频率 |
|------|------|------|------|
| `join` | C→S | — | 一次性 |
| `join_ack` | S→C | `{ playerId }` | 一次性 |
| `piece` | S→all | `{ pieceType }` | 锁块时 |
| `move` | C→all | `{ piece, x, y, r }` | 按需（变化时发送，超3秒心跳） |
| `attack` | C→S | `{ toId, lines }` | 消行时 |
| `garbage` | S→all | `{ toId, lines }` | Host 路由 |
| `board_delta` | C→S→all | 增量更新（锁块时） | ~20B |
| `board_full` | C→S→all | 全量快照（消行/garbage/心跳） | ~220B |
| `game_over` | C→S→all | `{ playerId }` | 死亡时 |
| `winner` | S→all | `{ playerId }` | 结束 |

### Move 消息策略

| 策略 | 说明 |
|------|------|
| 按需发送 | 位置/旋转变化时发送 move 消息 |
| 心跳保活 | 超过 3 秒无变化，强制发送一次 |
| 带宽估算 | 正常移动 ~600 B/s，最坏 < 1 KB/s |

**消息格式**：
```json
{ "type": "move", "piece": "T", "x": 3, "y": 14, "r": 1 }
```

### 棋盘同步：混合策略

| 触发条件 | 同步方式 | 数据量 | 说明 |
|----------|----------|--------|------|
| 方块锁定（无大消行） | **增量** | ~20 bytes | 仅锁定处4个格子变化 |
| 大量消行（Tetris/收到垃圾） | **全量** | ~220 bytes | 棋盘剧变，发送完整快照 |
| 每秒一次（心跳） | **全量** | ~220 bytes | 防止漂移，作为兜底 |

**增量格式**：`{ "type": "board_delta", "cells": [{ "x": 3, "y": 19, "color": 1 }, ...] }`（4个格子）

**全量格式**：`{ "type": "board_full", "cells": [0,0,1,1,...] }`（220字节）

### 简化点

- Host 只做 piece 分发 + 消息路由，不验证攻击
- Move 消息按需发送（变化时 + 3秒心跳），用于远程玩家预览
- board_delta/full 用于观众预览（玩家围观彼此棋盘）
- 断线玩家标记 inactive，重连从 Host 补发当前 piece

## UI 设计

### 分屏布局

屏幕：1280×720（横屏）。等宽竖分，不缩放格子尺寸。

| 玩家数 | 布局 | 每玩家宽度 | 格子尺寸 |
|--------|------|-----------|---------|
| 1 | 居中 | 单份 | 32px |
| 2 | **等宽左右** | 640px | 32px |
| 3 | **等宽左中右** | 427px | 24px |

> 三人时格子缩小至 24px，棋盘 10×20 = 240×480，占 427px 宽足够。
> 触屏按钮空间紧张，可缩小按钮尺寸或改为手势操作。
> 不再支持四人模式，保持实现简洁和观感统一。

### 屏幕元素布局（单人 -> 三人通用）

每个玩家的区域内：

```
┌──────────────────────┐
│  BOARD (10×20)       │  ┌──────────────┐
│                      │  │  ██ ██ ██ ██ │ ← NEXT 4 预览
│                      │  │     ...      │
│                      │  └──────────────┘
│                      │  攻击：其他       ← 信息栏
│                      │  连击x3
│                      │  垃圾+4
├──────────────────────┤
│  < > v V CW CCW H   │  ← 触屏按钮
└──────────────────────┘
```

### HOLD 实现方案

HOLD 不再使用独立存储区域。改为**与队列 peek(0) 交换**的机制：

- 每个玩家有私有 `m_holdSlot`，插在共享队列之前
- 按 H 时：当前块 ↔ holdSlot（首次 hold = 存当前块，取队列下一块）
- `nextPiece()` 优先消费 holdSlot，空了再走共享队列
- NEXT 预览区 slot 0 在有 hold 时自动显示被 hold 的块
- 视觉上 HOLD 和 NEXT 完全融合在同一个 4 格预览中

### 触屏按钮

- 保留在棋盘下方布局，直观易实现
- 移除暂停按钮（多人游戏无暂停）
- 7 个按钮：`< > v V CW CCW H`

### 视觉效果（标记 [后期]）

- 下落/旋转/边缘碰撞时窗口偏移震动效果
- 非简单闪烁，而是整体画面偏移后复位
- ⚠ 可能引发大量 LVGL 渲染重绘，需评估性能影响
- **当前优先级：低**，等核心功能稳定后再做

## 步骤

### Phase 1: 核心游戏引擎

1. **实现 `Piece` 类** (`tetris/piece.hpp/cpp`)
   - 7 种方块 (I/O/T/S/Z/J/L) 的 SRS 旋转状态
   - Wall Kick 表 (SRS) — 参考 [http://tetriswiki.cn/p/%E8%B6%85%E7%BA%A7%E6%97%8B%E8%BD%AC%E7%B3%BB%E7%BB%9F](http://tetriswiki.cn/p/%E8%B6%85%E7%BA%A7%E6%97%8B%E8%BD%AC%E7%B3%BB%E7%BB%9F)
   - `rotateCW()`, `rotateCCW()`, `move()`, `hardDrop()`, `softDrop()`

2. **实现 `Board` 类** (`tetris/board.hpp/cpp`)
   - 10×22 grid（可见 10×20）
   - `碰撞检测`、`放置`、`消行检测`
   - `addGarbage()` 垃圾行注入
   - `lockPiece()`, `getActivePiece()`

3. **实现 `PieceQueue` (7-bag randomizer)** + **Hold** + **Ghost**

4. **实现 `LockDelay` 计时器** (DAS/ARR 支持)

5. **实现 `Scoring` 系统** — Single/Double/Triple/Tetris + T-Spin + B2B

### Phase 2: 渲染 (LVGL)

> ⚠ **LVGL 线程安全**：`TetrisRenderer` 的渲染更新（创建/修改/删除任何 LVGL 对象）**必须在 `Display::LockGuard` 保护下执行**。推荐模式：
> ```cpp
> if (auto guard = display->lockGuard()) {
>     renderer.update(...);  // 所有 LVGL 操作在此内部
> }
> ```
> 仅在 LVGL 事件回调中可直接操作 UI（该上下文已持有锁）。

6. **HOLD+NEXT 混合** ✅ — 通过 holdSlot 与队列交换实现，无独立存储

7. **信息栏** ✅ — NEXT 面板下方独立区域：攻击目标、连击计数、垃圾提示

8. **Game Over 覆盖层** — 棋盘中央显示 GAME OVER（待实现）

9. **分屏布局** ✅ — 2人左右等宽（1人/3人待实现）

10. **攻击抵消机制** ✅ — 待处理垃圾队列，消行可抵消即将到来的垃圾

### Phase 3: 网络层 (WebSocket)

**优先级：高** — Web 端参与可极大提升调试效率，单人 PvE 开发也可独立推进。

11. **WebSocket Server** — 接入 `wsServer/wsServer.cpp`，Host 模式下运行
    - 消息类型：join / piece / attack / garbage / board_delta / board_full / game_over

12. **Web 调试客户端** — 简单的 HTML/JS 页面（`reserces/server/`），在浏览器中显示第二玩家的棋盘
    - 触屏按钮操作（手机上即可当手柄）
    - WebSocket 连接到 ESP32-P4

13. **局域网双人联机** — 一台 ESP32-P4 做 Host，另一台或 Web 页面做 Client

### Phase 4: 视觉特效 [后期]

14. **窗口偏移震动** — 方块锁定 / T-Spin / 边缘碰撞时，整体画面偏移后复位
    - ⚠ 可能引发大量 LVGL 渲染重绘，需评估性能影响
    - 当前优先级低，核心功能稳定后再做

   **设计目标**: 将所有输入来源（C6 手柄 / 触屏 / 按键 / Bot）统一为相同的接口，游戏逻辑只与 Input 层交互。

   ```cpp
   // 输入事件类型
   enum class InputAction {
       MoveLeft, MoveRight,   // 左/右移动
       RotateCW, RotateCCW,   // 顺/逆时针旋转
       SoftDrop, HardDrop,     // 软降/硬降
       Hold,                   // Hold
       Pause,                  // 暂停
   };

   // 输入源接口（各输入设备实现此接口）
   class IInputSource {
   public:
       virtual ~IInputSource() = default;
       virtual bool poll(InputAction* outAction) = 0;  // 非阻塞查询
       virtual void reset() = 0;                        // 重置输入状态
   };

   // 聚合输入管理器
   class InputManager {
   public:
       void addSource(IInputSource* source);  // 注册输入源
       void removeSource(IInputSource* source);
       void update();  // 每帧调用，合并所有来源的输入

       // 读取当前输入状态（DAS/ARR 由 InputManager 内部处理）
       bool isHeld(InputAction action) const;
       bool justPressed(InputAction action) const;
   };
   ```

   **实现**:
   - `ControllerInputSource` — C6 手柄输入，通过兼容层转换
   - `TouchInputSource` — 触屏虚拟按钮检测
   - `KeyboardInputSource` — 键盘（调试用）
   - `BotInputSource` — Bot 虚拟输入（后续接入）
   - `DASARRProcessor` — 内部模块，处理 DAS/ARR 时间管理

   **DAS/ARR 机制**:
   - DAS (Delayed Auto Shift): 按住方向键后延迟 $DAS$ ms 开始重复移动
   - ARR (Auto Repeat Rate): 重复移动间隔 $ARR$ ms
   - 典型值: $DAS = 170ms$, $ARR = 50ms$（可配置）

### Phase 4: 多人网络

10. **`TetrisNetManager` 类** (`tetris/net.hpp/cpp`)
    - 远程联机模式：Host 做 AP，Client 以 STA 模式连接，通过 WebSocket（端口 8080，路径 `/ws/tetris`）通信
    - 局域网联机：同一 WiFi 网络下通过 mDNS 发现 Host
    - WebSocket 消息解析与序列化
    - 状态同步 (每 4 帧广播一次，15fps)
    - 垃圾行计算与发送

11. **游戏房间**: 玩家数量检测、游戏开始同步

> **现有网络基础设施**：系统已有 HTTP server（端口 80）和 WebSocket server（端口 8080，`/ws/touch`、`/ws/stream`）。Phase 4 需在 `wsServer` 中注册新的 `/ws/tetris` handler，并将 `max_uri_handlers` 增至 4、`max_open_sockets` 增至 6 以容纳多人连接。

### Phase 5: 集成

12. **TetrisApp 实现** (`tetrisApp.hpp/cpp`):
    - `init()` — 初始化引擎 + 渲染 + 网络，启动独立 `Thread` 游戏线程（~60fps）
    - `deinit()` — 停止游戏线程、清理渲染、断开网络
    - 游戏线程循环：`处理输入 → 更新游戏逻辑 → LockGuard 渲染`

> **游戏循环方案**：选择 `Thread` 独立线程而非 `Task` 调度器，因游戏逻辑复杂度较高（碰撞检测、SRS 旋转、消行链、网络消息），需要稳定的独立栈空间和灵活的同步控制。`Thread` 封装了 FreeRTOS 任务创建，用法简洁。
>
> **注意**：`App` 基类提供 `init()`/`deinit()` 生命周期，无内置 `update()` 方法。游戏线程在 `init()` 中创建，在 `deinit()` 中停止并等待退出。

13. **App 切换**: 游戏 App 的启动（从桌面菜单进入）由外部 `Display::applyApp()` 负责，不属于 Tetris 模块职责。

### Phase 6: 测试 & 调优

14. **填充 Bot** (可选) — AI 代打
15. **速度曲线** — Guideline 级别递增
16. **音效** (可选) — `espressif__esp_dmx` 或简单 beep

## 关键文件

| 文件 | 用途 |
|------|------|
| `main/app/tetris/tetrisRoomApp.hpp/cpp` | 房间菜单 App |
| `main/app/tetris/tetrisApp.hpp/cpp` | 主游戏 App（分屏+Zoom） |
| `main/app/tetris/gameLogic/tetris_client.hpp/cpp` | 纯游戏逻辑 |
| `main/app/tetris/gameLogic/tetris_server.hpp/cpp` | 7-bag + 消息路由（仅 Host） |
| `main/app/tetris/renderer/tetris_renderer.hpp/cpp` | LVGL 渲染（与逻辑分离） |
| `main/app/tetris/net/tetris_net.hpp/cpp` | WebSocket 网络层 |
| `main/app/tetris/input/tetris_input.hpp/cpp` | 输入抽象层 |

## 依赖

- `lvgl__lvgl` (显示)
- `espressif__esp_new_jpeg` (ScreenStream MJPEG，可选串流)
- `espressif__esp_lcd_ili9881c` (屏幕驱动，同屏多人不需要)
- `espressif__esp_lcd_touch_gt911` (触摸)
- `espressif__freetype` (字体)
- `espressif__esp_wifi_remote` (C6 手柄通信)
- `espressif__mdns` (服务发现，局域网联机用)
- ESP-IDF `esp_http_server` (WebSocket 服务器，已内建)

## 后续计划

**Web 端支持**：同一套 WebSocket 服务器，可接入浏览器客户端（手机/PC）。利用 ESP32 内置 HTTP Server（port 80）托管 Web 静态资源，浏览器通过 WebSocket 连接游戏。

```
┌──────────────┐  ┌──────────────┐
│  Web 静态资源  │  │ WS Tetris    │
│  (HTML/JS)   │  │  客户端      │
└──────────────┘  └──────────────┘
```

**后续计划**：
- Web 浏览器游戏客户端（单页应用）
- 浏览器端调试工具（WebSocket 帧查看器）

## 验证

1. 单人模式: 方块旋转/移动/消行正常，分数正确
2. 双人对战: 垃圾行传递正确，一人死亡另一人胜
3. 分屏: 两人各占 640×720，画面独立；四人各占 640×360
4. 手柄: C6 摇杆/按键正确响应
5. 远程对战: Move/board 消息正确同步
6. 性能: 60fps 稳定，无帧丢失