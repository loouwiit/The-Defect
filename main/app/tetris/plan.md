# Tetris 多人游戏 — 架构文档

## 概述

ESP32-P4 上的 Guideline-compliant 现代俄罗斯方块，支持 1-3 人同屏对战。
主机全权模式 (Host-Authoritative)：所有游戏逻辑只运行在 Host 上，远程客户端只转发输入 + 收 snapshot 渲染。

**核心规则**:
- 棋盘: 10×20 可视，22 行高（含 2 行隐藏）
- SRS + Wall Kick
- 7-bag Randomizer（全局共享序列，每玩家独立游标）
- Hold（每块一次）
- Ghost Piece
- Lock Delay (500ms，移动/旋转重置)
- DAS (170ms) / ARR (50ms)
- T-Spin 判定：三角判定 + 旋转进入
- 消行计分 + Back-to-Back + Combo
- 垃圾行攻击（消行产生，跨玩家路由）

## 三层架构

```
GameState 是贯穿全系统的通用数据类型:

Input ──→ GameLogic ──→ GameState ──→ Renderer (LVGL)
              │                           │
              │ (Host)                    │ (Host & Client)
              ↓                           ↓
         Network(Host) ──────────────→ Network(Client)
           serialize                    deserialize
                                          ↓
                                      Renderer (Client)
```

| 层 | 文件 | 职责 |
|----|------|------|
| GameLogic | `tetris_client.hpp/cpp` | SRS/Board/Piece/Scoring/PieceQueue — 纯算法 |
| GameLogic | `player_state.hpp/cpp` | 单玩家游戏状态 + 流程控制 |
| GameState | `gameState.hpp` | 纯数据，三层之间的契约 |
| Renderer | `tetris_renderer.hpp/cpp` | LVGL 渲染，绑定 `const GameState*` |
| Network | `tetris_net.hpp/cpp` | WebSocket，仅 2 种消息 |
| App | `tetrisApp.hpp/cpp` | 游戏循环 + 初始化 |

## GameState — 数据契约

```
GameState
  ├── Board board             10×22 棋盘
  ├── currentPieceType/X/Y/R  当前方块
  ├── ghostPieceX/Y           下落预览位置
  ├── nextPieces[4]           预览队列
  ├── holdPiece/holdUsed      Hold
  ├── score/level/lines/combo 计分
  ├── pendingGarbage/flash    垃圾行
  └── gameOver/active         状态
```

## 主机全权模式

```
Host (ESP32-P4) 游戏循环 (60fps):

  1. tetrisNetUpdate()             接收远程输入
  2. for each player:
       processInput()               DAS/ARR/旋转
       updateGame()                 重力/LockDelay
       lockPiece() → spawnPiece()   锁块 + 从队列取下一块
  3. 跨玩家攻击路由
  4. exportState() → GameState
  5. TetrisRenderer::syncState()    diff 渲染
  6. tetrisNetHostSendSnapshot()    发给远程客户端

Client (Web / 另一台 P4):
  - 收 snapshot → 反序列化 → GameState → 渲染
  - 按键 → 发 input 消息 → 等待 Host 处理
```

## 出块队列

```
所有玩家共享同一个 7-bag 序列，但各有独立游标：

  序列: [T, S, Z, J, O, L, I, Z, T, J, ...]
           ↑        ↑
        P0 游标    P1 游标

PieceQueue::peek(index)  按绝对索引取块，链表自增长
PlayerState::nextPiece()  m_queue->peek(m_pieceIndex++)
```

链表节点动态分配（`heap_caps_malloc(PSRAM)`），永不回收。1000 块 ≈ 1.6KB，一场游戏绰绰有余。

## 消息协议（仅 2 种）

```
C→S:  {"t":"input","k":"left","d":true}
S→C:  {"t":"snapshot","p":0,"b":[...200B...],"pt":"T","px":3,...}
```

## 渲染

- `TetrisRenderer` 绑定 `GameState*`，不依赖 `PlayerState`（仅按钮回调除外）
- `syncState()` 做增量 diff（比较 `m_prevState` 与当前 GameState）
- 棋盘：遍历 200 格，`m_visualCache` 比较后更新
- Ghost：比较 (X, Y, Rotation) 三元组决定是否重绘
- 预览：基于每玩家游标 + Hold 槽计算

## 文件结构

```
main/app/tetris/
├── plan.md                        ← 本文档
├── tetrisApp.hpp/cpp              主游戏 App
├── gameLogic/
│   ├── tetris_client.hpp/cpp      核心算法 (SRS/Board/Piece/Scoring/Queue)
│   ├── player_state.hpp/cpp       单玩家状态 + 流程
│   └── gameState.hpp              数据契约
├── renderer/
│   └── tetris_renderer.hpp/cpp    LVGL 渲染
└── net/
    └── tetris_net.hpp/cpp         WebSocket 网络层
```

## 依赖

- `lvgl__lvgl` — 显示
- `espressif__freetype` — 字体
- `espressif__esp_new_jpeg` — ScreenStream MJPEG
- ESP-IDF `esp_http_server` — WebSocket 服务器
- ESP-IDF `cJSON` — 消息序列化

## 待办

- [ ] 胜者检测（只剩一个存活时宣布赢家）
- [ ] 攻击路由策略（目前固定攻击下家）
- [ ] Game Over 覆盖层
- [ ] 房间菜单 (`tetrisRoomApp`)
- [ ] Web 调试客户端 (`reserces/server/tetris.html`)