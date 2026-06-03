# Plan: Multiplayer Tetris App

## 概述

在 ESP32-P4 上实现一个 **Guideline-compliant 现代俄罗斯方块**多人游戏，支持 1-4 人同屏对战（双人为 1:1 分屏，更多人则缩小显示）。玩家使用 ESP32-C6 无线手柄或触屏操作，主机通过 WiFi Direct 互联传递垃圾行。

**核心规则**（基于 Tetris Guideline 2006）:
- 棋盘: 10×20（可视），隐藏区 22 行高
- SRS (Super Rotation System) + Wall Kick
- 7-bag Randomizer
- Hold 功能（每块一次）
- Ghost Piece
- Lock Delay (约 500ms)
- DAS/ARR 移动
- 消行计分 & Back-to-Back T-Spin
- 垃圾行机制（Battle Royale 核心）

**多人模式**: Battle Royale — 每人独立棋盘，消行产生垃圾行发送给其他玩家，最后存活者胜。

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

## 分屏布局

屏幕：1280×720（横屏）。2人左右分割，4人2×2网格，3人先支持2人开局后续加入。

| 玩家数 | 布局 | 每玩家分辨率 | 每格像素 |
|--------|------|-------------|----------|
| 1 | 独立全屏 | 1280×720 | ~32px |
| 2 | **左右分屏** | 640×720 | ~32px |
| 3 | 左 640×720 + 右上下各 320×360 | 640×720 / 320×360 | 32/16px |
| 4 | **2×2 网格** | 640×360 | ~16px |

> 分屏通过 LVGL container 实现，各 player 独立 `TetrisRenderer`。
> 3人时右半屏上下分割，格子仅16px，拥挤。后续可优化隐藏Next/Hold区或缩小字体。

## 步骤

### Phase 1: 核心游戏引擎

1. **实现 `Piece` 类** (`tetris/piece.hpp/cpp`)
   - 7 种方块 (I/O/T/S/Z/J/L) 的 SRS 旋转状态
   - Wall Kick 表 (SRS)
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

6. **`TetrisRenderer` 类** (`tetris/renderer.hpp/cpp`)
   - `drawBoard(playerIndex)` — 绘制玩家棋盘
   - `drawNext(queue)` — 下一个方块预览
   - `drawHold(piece)` — Hold 框
   - `drawGhost()` — 幽灵块
   - `drawStats(score, level, lines)` — 分数/等级/消行
   - `drawGarbage(pending)` — 等待的垃圾行指示

7. **分屏布局逻辑** — 根据玩家数设置各 renderer 区域

8. **连击/消行特效** — LVGL 动画（闪烁、震动）

### Phase 3: 输入抽象层

9. **`TetrisInput` 输入抽象层** (`tetris/input.hpp/cpp`)

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
    - WiFi Direct 连接（作为 AP 或 STA）
    - WebSocket `tetris` 路径消息处理
    - 状态同步 (每 4 帧广播一次，15fps)
    - 垃圾行计算与发送

11. **游戏房间**: 玩家数量检测、游戏开始同步

### Phase 5: 集成

12. **App 接口** (`tetris/app.hpp`):
    - `init()` — 初始化引擎 + 渲染 + 网络
    - `deinit()` — 停止所有任务
    - `update()` — 主循环 (60fps 逻辑更新)

13. **主菜单**: 开始游戏、加入房间、设置

### Phase 6: 测试 & 调优

14. **填充 Bot** (可选) — AI 代打
15. **速度曲线** — Guideline 级别递增
16. **音效** (可选) — `espressif__esp_dmx` 或简单 beep

### Phase 7（未来计划）: Web 端接入

由于使用 WebSocket 协议 + HTTP 服务器，天然支持 Web 客户端接入：

- **目标**：手机/PC 浏览器作为 Player 3/4 加入房间
- **优势**：零成本扩展玩家数量，无需额外硬件
- **实现路径**：
  - 在 port 80 的 HTTP 服务器托管 Web 客户端（HTML + JS）
  - 浏览器通过 WebSocket 连接 `/ws/tetris` 加入房间
  - 复用现有消息协议（piece/move/attack/garbage/board_*）
- **附加能力**：
  - 浏览器端调试工具（直接看 WebSocket 帧、状态）
  - 观众/围观模式（只读 board_full）
  - 跨平台支持（任何带浏览器的设备）

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
- `espressif__esp_new_jpeg` (ScreenStream MJPEG)
- `espressif__esp_lcd_ili9881c` (屏幕驱动)
- `espressif__esp_lcd_touch_gt911` (触摸)
- `espressif__freetype` (字体)
- `espressif__esp_wifi_remote` (WiFi Direct)

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
3. 分屏: 两人各占 640×360，画面独立
4. 手柄: C6 摇杆/按键正确响应
5. 远程对战: Move/board 消息正确同步
6. 性能: 60fps 稳定，无帧丢失