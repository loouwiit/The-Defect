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
├── tetrisApp.hpp/cpp         ← 主游戏（分屏 + 网络模式）
├── gameLogic/
│   ├── tetris_client.hpp/cpp    ← 纯游戏逻辑（Board/Piece/Queue/Hold/Scoring）
│   └── player_state.hpp/cpp    ← 单玩家状态 + 游戏逻辑方法
├── renderer/
│   └── tetris_renderer.hpp/cpp ← LVGL 渲染
├── net/
│   └── tetris_net.hpp/cpp      ← WebSocket 网络层（通过 wsServer 注册机制）
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

### Phase 1: 核心游戏引擎 ✅

1. ✅ **`Piece` 类** — SRS 旋转 + Wall Kick
2. ✅ **`Board` 类** — 10×22 碰撞/消行/垃圾行
3. ✅ **`PieceQueue`** — 7-bag linked list
4. ✅ **LockDelay / DAS/ARR**
5. ✅ **`Scoring`** — Single/Double/Triple/Tetris + T-Spin + B2B + Combo

### Phase 2: 渲染 (LVGL) ✅

6. ✅ **HOLD+NEXT 混合** — holdSlot 交换
7. ✅ **信息栏** — NEXT 下方：攻击/连击/垃圾提示
8. ⬜ **Game Over 覆盖层** — 棋盘中央显示 GAME OVER
9. ✅ **分屏布局** — 2人左右等宽
10. ✅ **攻击抵消机制** — 待处理垃圾队列 + 消行抵消

### Phase 3: 网络层 (WebSocket)

**优先级：高** — Web 端参与可极大提升调试效率。

#### 3.1 基础设施改造 (`wsServer`)

扩展现有 `wsServer`（端口 8080）以支持 Tetris WebSocket 消息。

**现有状态**：
- 使用 `esp_http_server`（`httpd`）
- 当前配置：`max_uri_handlers=2`（`/ws/touch` + `/ws/stream`），`max_open_sockets=4`
- `s_server` handle 是文件内 `static`，外部不可访问

**改动项**：

| # | 改动 | 文件 | 说明 |
|---|------|------|------|
| 1 | `max_uri_handlers` 2→4 | `wsServer.cpp` | 为 tetris handler 留空间 |
| 2 | `max_open_sockets` 4→6 | `wsServer.cpp` | Host(1) + 2 clients + touch + stream + 1余量 |
| 3 | 导出 `wsServerGetHandle()` | `wsServer.hpp/cpp` | 返回 `s_server` |
| 4 | 导出 `wsServerSendText()` | `wsServer.hpp/cpp` | 发送 UTF-8 文本帧到指定 fd |
| 5 | 导出 `wsServerBroadcastText()` | `wsServer.hpp/cpp` | 广播文本帧到所有匹配 URI 的客户端 |

#### 3.2 Tetris 网络模块 (`net/tetris_net`)

**文件结构**：
```
app/tetris/net/
├── tetris_net.hpp         ← 公共接口 + 消息类型定义
├── tetris_net_host.cpp    ← Host 端实现（消息路由 + 出块分发）
└── tetris_net_client.cpp  ← Client 端实现（连接 + 收发）
```

**接口设计**：

```cpp
// tetris_net.hpp

/// 消息类型
enum class TetrisMsgType : uint8_t {
    JOIN, JOIN_ACK, PIECE, LOCK,
    ATTACK, GARBAGE,
    BOARD_DELTA, BOARD_FULL, MOVE,
    GAME_OVER, WINNER,
    PING, PONG,
};

/// Host 回调（TetrisApp 注册）
struct TetrisHostCallbacks {
    std::function<void(int playerId)> onJoin;
    std::function<void(int playerId, int lines)> onAttack;
    std::function<void(int playerId)> onGameOver;
};

/// Client 回调（TetrisApp 注册）
struct TetrisClientCallbacks {
    std::function<void(int playerId)> onJoined;
    std::function<void(PieceType type)> onNewPiece;
    std::function<void(int lines)> onGarbage;
    std::function<void(int playerId)> onGameOver;
    std::function<void(int playerId)> onWinner;
};
```

**消息格式（cJSON 文本）**：

```json
JOIN:        C→S     { "type":"join" }
JOIN_ACK:    S→C     { "type":"join_ack", "player_id":1 }
PIECE:       S→all   { "type":"piece", "piece":"T" }
LOCK:        C→S     { "type":"lock" }
ATTACK:      C→S     { "type":"attack", "target":0, "lines":2 }
GARBAGE:     S→C     { "type":"garbage", "lines":3 }
MOVE:        C→all   { "type":"move", "piece":"T", "x":3, "y":14, "r":1 }
BOARD_DELTA: C→all   { "type":"board_delta", "cells":[[3,19,4],[4,19,4]] }
BOARD_FULL:  C→all   { "type":"board_full", "cells":[0,0,1,1,...] }
GAME_OVER:   C→S     { "type":"game_over" }
WINNER:      S→all   { "type":"winner", "player_id":0 }
PING:        C→S     { "type":"ping" }
PONG:        S→C     { "type":"pong" }
```

**消息序列**：

```
Host (ESP32-P4, Player 0)          Client (Web / 另一台 P4)
     │                                      │
     │◄──── WS connect (/ws/tetris) ────────│
     │◄─────── JOIN ────────────────────────│
     │────── JOIN_ACK {player_id:1} ────────►│
     │                                      │
     │────── PIECE {piece:T} ──────────────►│  ← Host 分发
     │   (本地玩家也取同一共享队列)           │
     │                                      │
     │  双方各自本地游戏逻辑                  │
     │                                      │
     │◄────── LOCK ─────────────────────────│  ← Client 锁块
     │────── PIECE {piece:S} ──────────────►│  ← Host 发下一块
     │                                      │
     │◄─── ATTACK {target:0, lines:2} ─────│  ← Client 消行
     │───── GARBAGE {lines:2} ─────────────►│  ← Host 路由
     │                                      │
     │◄────── GAME_OVER ───────────────────│
     │────── WINNER {player_id:0} ─────────►│
```

> **关键设计**：Host 内置 Player 0，统一维护 7-bag。Client 锁块时通知 Host，Host 从同一队列取下一块分发。这保证了**分屏玩家和远程玩家看到的是同一出块序列**——和当前双人分屏的 `m_sharedQueue` 一致。

#### 3.3 实现步骤

| 步骤 | 内容 | 文件 | 工作量 |
|------|------|------|--------|
| **3.3.1** | wsServer 改造：增大限制、导出 handle 和发送函数 | `wsServer.hpp/cpp` | 小 |
| **3.3.2** | 定义消息协议常量 + cJSON 序列化/反序列化工具 | `tetris_net.hpp` + 新文件 | 小 |
| **3.3.3** | 实现 `TetrisNetHost`：连接管理 + 消息路由 + piece 分发 | `tetris_net_host.cpp` | 中 |
| **3.3.4** | 实现 `TetrisNetClient`：连接 + 收发 + 回调 | `tetris_net_client.cpp` | 中 |
| **3.3.5** | TetrisApp 网络集成：netMode 分支 + 网络版游戏循环 | `tetrisApp.hpp/cpp` | 中 |
| **3.3.6** | TetrisRoomApp 房间菜单：LVGL UI + 模式选择 + IP 输入 | `tetrisRoomApp.hpp/cpp` | 中 |
| **3.3.7** | Web 调试客户端：Canvas 渲染棋盘 + WS 通信 | `reserces/server/tetris.html` | 大 |
| **3.3.8** | 端到端联调：本地 Host + Web Client 双人对战 | — | 中 |
| **3.3.9** | 双 ESP32-P4 局域网联机测试 | — | 中 |

> 3.3.1-3.3.5 完成后即实现最简联机，3.3.6 和 3.3.7 可并行开发。

#### 3.4 游戏房间流程

```
┌──────────┐     ┌──────────┐     ┌──────────┐
│ 房间菜单  │────►│  等待室   │────►│  游戏中   │
│ 3.3.6    │     │ (等待)   │     │ (对战)    │
└──────────┘     └──────────┘     └──────────┘
```

**参与模式**：

| 模式 | 描述 |
|------|------|
| **本地双人** | 当前实现，2人同屏分屏 |
| **Host 模式** | 本机为 Player 0，接受远程 Client 加入 |
| **Client 模式** | 本机连接到远程 Host，作为 Player 1 |

> 初期简化：Host 模式下本地固定为 Player 0，接受 1 个远程 Client。
> 后续扩展：Host 可接受 2 个 Client（3人模式）。

#### 3.5 游戏循环（网络版）

```
每帧 (16ms):
  1. processInput() for each local player
  2. updateGame() for each local player
  3. net->update()           ← 处理接收队列
  4. 跨玩家攻击路由（本地 + 远程）
     - 本地攻击 → netHost.routeAttack()
     - 远程攻击 → netClient.sendAttack()
  5. LockGuard 渲染
```

#### 3.6 TetrisRoomApp — 房间菜单 UI

```
┌────────────────────────────────────────┐
│            🎮 俄罗斯方块                  │
│                                        │
│    ┌──────────────────────────┐        │
│    │    🏠 本地双人游戏        │        │
│    └──────────────────────────┘        │
│    ┌──────────────────────────┐        │
│    │    🌐 创建房间 (Host)     │        │
│    └──────────────────────────┘        │
│    ┌──────────────────────────┐        │
│    │    🔗 加入远程房间        │        │
│    └──────────────────────────┘        │
│                                        │
│    Host IP: 192.168.x.x                │
│    等待玩家加入...                      │
└────────────────────────────────────────┘
```

### Phase 4: 输入抽象层

**优先级：中** — 当前触屏直接写 `PlayerState.key*` 工作良好，但未来接入 C6 手柄和网络输入时需要统一接口。

```cpp
// app/tetris/input/tetris_input.hpp

enum class InputAction {
    MoveLeft, MoveRight,
    RotateCW, RotateCCW,
    SoftDrop, HardDrop,
    Hold,
};

class IInputSource {
public:
    virtual ~IInputSource() = default;
    virtual bool poll(InputAction* outAction) = 0;
    virtual void reset() = 0;
};

class InputManager {
public:
    void addSource(IInputSource* source);
    void removeSource(IInputSource* source);
    void update();
    bool isHeld(InputAction action) const;
    bool justPressed(InputAction action) const;
};
```

**实施顺序**：

| 步骤 | 内容 | 工作量 |
|------|------|--------|
| 4.1 | 定义 InputAction + IInputSource + InputManager | 小 |
| 4.2 | 实现 TouchInputSource（封装现有触屏按钮） | 中 |
| 4.3 | 实现 NetworkInputSource（WS 按键消息转 InputAction） | 中 |
| 4.4 | TetrisApp 切换到 InputManager 模式 | 中 |
| 4.5 | 实现 ControllerInputSource（C6 手柄） | 中 |

### Phase 5: 视觉特效 [后期]

| 步骤 | 内容 | 优先级 |
|------|------|--------|
| 5.1 | 窗口偏移震动 — 方块锁定 / T-Spin 时画面偏移复位 | 低 |
| 5.2 | 消行动画 — 消行时行闪烁后消失 | 低 |

### Phase 6: 测试 & 调优

| 步骤 | 内容 | 优先级 |
|------|------|--------|
| 6.1 | 填充 Bot — AI 代打 | 可选 |
| 6.2 | 速度曲线 — Guideline 级别递增 | 中 |
| 6.3 | 音效 — `esp_dmx` 或简单 beep | 可选 |
| 6.4 | Game Over 覆盖层 — 棋盘中央显示 GAME OVER | 低（可提前到 Phase 2） |

## 关键文件

| 文件 | 用途 |
|------|------|
| `main/app/tetris/tetrisRoomApp.hpp/cpp` | 房间菜单 App |
| `main/app/tetris/tetrisApp.hpp/cpp` | 主游戏 App（分屏+网络模式） |
| `main/app/tetris/gameLogic/tetris_client.hpp/cpp` | 纯游戏逻辑 |
| `main/app/tetris/renderer/tetris_renderer.hpp/cpp` | LVGL 渲染 |
| `main/app/tetris/net/tetris_net.hpp` | 网络层公共接口 |
| `main/app/tetris/net/tetris_net_host.cpp` | Host 网络实现 |
| `main/app/tetris/net/tetris_net_client.cpp` | Client 网络实现 |
| `main/app/tetris/input/tetris_input.hpp/cpp` | 输入抽象层 |

## 依赖

- `lvgl__lvgl` (显示)
- `espressif__esp_new_jpeg` (ScreenStream MJPEG)
- `espressif__freetype` (字体)
- `espressif__esp_wifi_remote` (C6 手柄通信)
- `espressif__mdns` (服务发现)
- ESP-IDF `esp_http_server` (WebSocket 服务器，已内建)
- ESP-IDF `cJSON` (消息序列化，已内建)

## 现有基础设施

### WebSocket Server (`wsServer`)

```
端口: 8080
当前 URI:
  /ws/touch   — 触屏输入回传（12字节二进制）
  /ws/stream  — MJPEG 屏幕串流
待添加:
  /ws/tetris  — 游戏消息（JSON 文本）
```

### HTTP Server (`serverKernal`)

```
端口: 80
托管: reserces/server/ 目录下的静态文件
用途: 提供 Web 客户端 HTML/JS 资源
```

### 配置限制调整

| 参数 | 当前值 | 调整后 |
|------|--------|--------|
| `max_uri_handlers` | 2 | 4 |
| `max_open_sockets` | 4 | 6 |

## 验证

1. 单人模式: 方块旋转/移动/消行正常，分数正确
2. 双人对战: 垃圾行传递正确，一人死亡另一人胜
3. 分屏: 两人各占 640×720，画面独立
4. 手柄: C6 摇杆/按键正确响应
5. 远程对战 (Host+Web): Web 端成功连接、收发消息、接收垃圾行
6. 远程对战 (P4+P4): 双机联机垃圾行路由正确
7. 性能: 60fps 稳定，无帧丢失