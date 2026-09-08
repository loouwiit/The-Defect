# 软件架构总览

## 技术栈

| 层次 | 技术 |
|------|------|
| 操作系统 | FreeRTOS（双核，1000Hz 节拍） |
| 开发框架 | ESP-IDF v5.5.2 |
| UI 框架 | LVGL 9.5.0 |
| 字体渲染 | FreeType（通过 LVGL VFS 读取 FAT 分区字体） |
| 蓝牙协议栈 | NimBLE（运行在 ESP32-C6 上，通过 esp_hosted SDIO 通信） |
| 网络 | lwIP WiFi（STA/AP + NAT）+ mDNS |
| Lua | Lua 5.5（沙箱化，安全标准库） |
| AI | OpenAI 兼容 HTTPS API |

## 模块架构

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (App)                           │
│  DesktopApp │ Settings │ Market │ AI │ LuaManager │ ...  │
├─────────────────────────────────────────────────────────┤
│               ELF 插件层 (.so)                            │
│  snake │ chineseChess │ fruitNinja │ tetris │ douDiZhu  │
├─────────────────────────────────────────────────────────┤
│                Lua 引擎                                   │
│  LuaRuntime │ luaDisplay │ luaInput │ luaAudio │ ...    │
├─────────────────────────────────────────────────────────┤
│                AI Agent                                   │
│  AiClient │ AiParser │ AiExecutor │ AiAgentOverlay      │
├─────────────────────────────────────────────────────────┤
│                导航系统                                    │
│  AppStackManager → AppStack → App                        │
├─────────────────────────────────────────────────────────┤
│                显示与输入                                  │
│  Display │ ILI9881c │ FontLoader │ GT911Indev │ Touch   │
├─────────────────────────────────────────────────────────┤
│                网络与通信                                  │
│  WiFi │ mDNS │ HTTP Server │ WS Server │ WsClient       │
├─────────────────────────────────────────────────────────┤
│                外设驱动                                   │
│  ES8311 │ BatteryManager │ BleGamepad │ ScreenStream    │
├─────────────────────────────────────────────────────────┤
│                基础设施                                    │
│  Task │ Mutex │ GPIO │ I²C │ Storage │ OtaManager       │
├─────────────────────────────────────────────────────────┤
│                ESP-IDF v5.5.2 + FreeRTOS                 │
└─────────────────────────────────────────────────────────┘
```

## 核心模块说明

### 导航系统 (AppStack)

应用栈管理器（`AppStackManager`）是系统级单例，管理多个 `AppStack`：

```
AppStackManager
├── Stack 0 [Desktop]        ← 根栈（桌面，常驻）
├── Stack 1 [Game, ...]      ← 游戏独立栈
└── activeStack              ← 当前前台栈
```

- **push**：新 App 压入当前栈顶，旧 App 保留在栈底
- **pop**：弹出栈顶 App，恢复下层 App
- **replace**：替换当前栈顶
- **pushToNewStack**：创建新栈并压入（游戏通常用此方式）

### App 基类

所有应用继承自 `App`，关键虚方法：

| 方法 | 说明 |
|------|------|
| `init()` | 初始化 UI 和后台线程 |
| `deinit()` | 清理资源 |
| `onForeground()` | 成为前台 App |
| `onBackground()` | 被覆盖到后台 |
| `onGamepadInput()` | 收到手柄输入 |
| `getAiContext()` | 返回 App 描述（给 AI） |
| `executeAiAction()` | 执行 AI 动作 |

### ELF 插件系统

游戏编译为 `.so` 动态库，通过 `dlopen` 运行时加载：

```
主固件符号表 (g_main_elfsyms)
    ↓ 提供
lv_* / esp_* / cJSON_* / FreeRTOS / std::new 等
    ↓ 被引用
.so 插件 (elf_plugin_entry → create_app → new XxxElfApp)
    ↓ 继承
App 基类 (虚分派回 .so 代码)
```

### Lua 引擎

沙箱化 Lua 5.5 VM，每个游戏独立实例：

```
LuaGameApp (C++ 驱动 60fps 游戏循环)
  ├── LuaRuntime (VM RAII 封装)
  │   ├── luaDisplay (LVGL 对象池化绑定)
  │   ├── luaInput (手柄帧快照)
  │   ├── luaTouch (多点触控轮询)
  │   ├── luaAudio (音效播放)
  │   ├── luaStorage (安全存档)
  │   └── luaThread (延时/随机)
  └── setup() / loop(dt) ← Lua 脚本必须实现
```

### AI Agent

多轮对话 + 动作执行：

```
用户输入 → AiClient (HTTPS) → AI 响应
  → AiParser (JSON 解析)
  → AiExecutor (14 个系统动作)
  → nextRound? → 继续对话（最多 5 轮）
  → memory → 长期记忆存储
```

### VirtualGamepad（统一手柄抽象层）

系统级单例，汇集所有输入源：

```
BLE Task → inject(0, p, state)      ← 本机手柄
UDP Task → inject(hostId, p, state) ← 远程 P4
WS Task  → inject(hostId, p, state) ← Web 客户端
                    ↓
VirtualGamepad (60fps Dispatch Task)
                    ↓
app->onGamepadInput(hostId, playerId, state)
```

- 掩码过滤：每个 hostId/playerId 可独立启用/禁用
- 门控：AI Overlay 或低电量弹窗可见时跳过分发

## 启动流程

完整的 `app_main()` 启动顺序：

| 阶段 | 操作 | 说明 |
|------|------|------|
| 1. 基础设施 | `Task::init()` → `nvsInit()` → 存储挂载 | 协程调度器、NVS、FAT/SD |
| 2. 日志 | `LogService::start()` → `detectCrash()` | 日志重定向 + 崩溃检测 |
| 3. ELF SDK | `elf_sdk_init()` | 注册符号表（必须在 dlopen 前） |
| 4. 协处理器 | `Slave::start()` | 后台启动 C6 SDIO 复位 |
| 5. 显示 | `Display::init()` → `ILI9881c::init()` → `GT911Indev::start()` | LVGL + 面板 + 触摸 |
| 6. 串流 | `ScreenStream::start()` | MJPEG 硬件编码 |
| 7. 字体 | `FontLoader::load()` × 3 | 24/32/56 三字号 |
| 8. 音频 | `ES8311::init()` → `Audio::init()` | 编解码器 + 混音器 |
| 9. 电池 | `BatteryManager::init()` | ADC 初始化 |
| 10. 应用栈 | `AppStackManager::init()` → `createStack()` → `push(DesktopApp)` | 桌面启动 |
| 11. BLE | `BleGamepad::start()` | 手柄管理器 |
| 12. AI | `AiAgentOverlay::start()` | AI 浮动窗 |
| 13. 电池监控 | `BatteryMonitor::start()` | 低电量检测 |
| 14. WiFi | `wifiInit()` → `wifiStart()` → 连接或降级 AP | 网络连接 |
| 15. mDNS | `mdnsInit()` → `mdnsStart()` → 注册服务 | 设备发现 |
| 16. 服务器 | `OtaManager()` → `serverStart()` → `wsServerStart()` | HTTP + WS |
| 17. 联机 | `udpInputStart()` → `wsConsoleHandlerRegister()` | UDP + 控制通道 |

## 线程模型

| 线程 | 优先级 | 用途 |
|------|--------|------|
| LVGL Worker | Normal | LVGL 渲染循环 |
| Task Daemon ×2 | Deamon(2) | 协程调度器 |
| BLE Task | — | NimBLE 协议栈 |
| VirtualGamepad Dispatch | Normal | 60fps 手柄分发 |
| HTTP Workers ×6 | Normal | HTTP 请求处理 |
| WS Worker | RealTime | WebSocket 处理 |
| Log Task | Deamon | 日志写入 |
| ScreenStream Encode | Normal | JPEG 编码 |
| ScreenStream Send | Normal | WS 帧发送 |
| OTA Worker | Internal RAM | 固件写入 |
| ELF Loader | Internal RAM | 插件 dlopen |
| UDP Input | Normal | P4→P4 输入接收 |
