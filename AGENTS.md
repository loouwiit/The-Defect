# The-Defect — Agent Guidelines

## ⚠ AGENTS.md 维护约定

本仓库的 AI 协作指南采用「**草稿 + 发布**」双文件工作流：

- **`AGENTS.md`（发布版）** — 被所有 AI 会话加载的正式指南。**避免中途修改**：其他 AI 可能正
  依赖其内容作为缓存/上下文，半中间改动会造成**上下文漂移与缓存失效**。
- **`newAGENTS.md`（草稿版）** — 所有对指南的修改**必须先应用到这里**，经人工确认
  无误后再整体替换 `AGENTS.md`（`mv newAGENTS.md AGENTS.md`，或由用户用全局替换工具完成）。

**工作流约定（对 AI）**：
1. 代码/架构发生变更时，先更新 `newAGENTS.md` 中对应章节（`AGENTS.md` 保持不动）
2. 汇总本次变更内容，向用户报告并请求确认
3. 用户确认后，由用户（或经用户同意）将 `newAGENTS.md` 整体替换 `AGENTS.md`

**⚠ 检查提醒（对 AI）**：若发现工作区**没有 `newAGENTS.md`**（或内容明显落后于 `AGENTS.md`），
请**主动提醒用户创建/同步一份**再继续——避免后续修改直接落到 `AGENTS.md`，造成缓存失效与上下文变化。

---

## Project Overview

ESP32-P4 驱动的多人游戏主机 + ESP32-C6 无线手柄。屏幕为 6寸 720×1280 MIPI ILI9881C，触摸 GT911 (I²C)，GUI 使用 LVGL 9.5.0。

## Build & Flash

使用 `@espIdfCommands` 工具（ESP-IDF 扩展）进行编译、烧录、监控等操作，不要直接执行 bash 命令。

## Partition Table

```
# Name,   Type, SubType, Offset, Size,    Flags
nvs,      data, nvs  ,         , 16k
otadata,  data, ota  ,         ,  8k,
phy_init, data, phy  ,         ,  4k,
ota_0,    app , ota_0,         ,  6M
ota_1,    app , ota_1,         ,  6M
coredump, data, coredump,       , 512K,
fat,      data, fat  ,      16M, 16M,
```

分区说明：
| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 16KB | NVS 键值持久化（WiFi 配置、手柄配对、音量、mDNS 主机名、时间自动同步开关、**OTA 回滚记录**、**崩溃统计**等） |
| otadata | 8KB | OTA 启动选择数据 |
| phy_init | 4KB | WiFi/BT 物理层校准数据 |
| ota_0 / ota_1 | 6MB × 2 | 双 OTA 应用槽（交替升级，bootloader 自动回滚安全网） |
| coredump | 512KB | 崩溃转储（自动位于 ota_1 后空隙 0xc20000；fat 偏移不变，数据零迁移）。下载：`GET /api/coredump`，解码：`idf.py coredump-info -c core.elf <build/The-Defect.elf>` |
| fat | 16MB | FAT 数据分区，挂载到 `/root/`（偏移固定 16M，OTA 不影响数据） |

> ⚠ 完整板已封壳（无 USB 调试口），**唯一升级途径是 OTA**：浏览器 `http://<主机名>.local/server/ota/index.html` 或 `curl -X POST --data-binary @build/The-Defect.bin http://<主机名>.local/api/ota`。升级镜像 = `build/The-Defect.bin`（app 镜像，非 merge 整片）。分区表/bootloader 变更需在封壳前一次性 USB 全量刷写（OTA 无法改分区表）。

## Resources

`resourse/` 目录下的文件会被上传到 FAT 分区，挂载路径为 `/root/`。

```
resourse/
├── lua/              ← Lua 测试/示例脚本
├── server/          ← HTTP 服务器静态资源（端口 80）
│   ├── index.html
│   ├── index.css
│   ├── screen/      ← 屏幕串流 Web 查看器
│   ├── tetris/      ← Tetris 网页客户端
│   ├── gamepad/     ← 虚拟手柄 Web 客户端（/ws/input 测试）
│   ├── wifi/        ← WiFi 网页配置
│   ├── ota/         ← OTA 升级页面
│   └── diag/        ← 系统诊断页面（实时日志 / 崩溃状态 / 日志级别）
└── system/          ← 系统文件（字体 + 输入法词库）
    ├── NotoSC.ttf   ← 全局默认字体
    ├── pinyin_dict.txt      ← 拼音词库（411 码 / 8343 字，Rime 8105 规范汉字表）
    ├── wubi_dict.txt        ← 五笔词库（21812 码 / 26414 字，Rime 五笔86极点码表）
    └── wubi_pinyin_dict.txt ← 五笔+拼音混合词库（22088 码 / 25042 字，五笔优先拼音兜底）
```

---

## Architecture — 完整模块概览

| 层次 | 模块 | 路径 | 职责 |
|------|------|------|------|
| **基础设施** | Task | `main/task/` | 协程调度器（轮询）+ Thread 封装，Priority/Affinity 管理 |
| | Mutex | `main/mutex/` | FreeRTOS `xSemaphoreCreateMutex` RAII 封装 (`Mutex` + `Lock`) |
| | GPIO | `main/gpio/` | GPIO 装饰器封装，`=` / `bool` / `int` 运算符重载，ISR 注册 |
| | IIC | `main/iic/` | I²C 主控总线 (`IIC`) + 设备封装 (`IICDevice`)，自动端口选择 |
| | VirtualGamepad | `main/gamepad/virtualGamepad.hpp` | 统一手柄输入抽象层，60fps Dispatch + hostMask 掩码 + 远程转发 |
| | TimeService | `main/time/timeService.hpp` | 全局 SNTP 时间同步服务：自动同步 NVS 持久化 + WiFi 连接自动同步 + 周期校正 + 句柄化多回调 |
| | **LogService** | `main/logService/` | 日志重定向（`esp_log_set_vprintf` → FAT 文件 + `/ws/log` 实时推送）+ 日志级别 NVS 持久化 + 崩溃检测记录 |
| **显示/输入** | Display | `main/display/display.hpp` | ILI9881C 初始化 + LVGL 适配层，`LockGuard` RAII（递归锁） |
| | ILI9881c | `main/display/ili9881c.cpp` | 面板驱动（MIPI DSI），背光亮度控制 |
| | FontLoader | `main/display/font.cpp` | FreeType 字体加载，3 字号全局默认字体，VFS 驱动器 'F' |
| | Touch | `main/touch/` | GT911 驱动 (I²C)，`Touch::update()` 读全 5 点含 track_id |
| | GT911Indev | `main/touch/gt911Indev.hpp` | **五 indev 多指桥接层**：INT 中断驱动 → track_id→槽位路由 → 5 个独立 lv_indev（POINTER），release 握手防 track_id 复用跳变 |
| | VirtualIndev | `main/virtualIndev/` | 虚拟触摸输入设备（Web 触摸注入），无锁去重 + hasNewData/continue_reading |
| **导航** | App | `main/app/app.hpp` | 应用基类（生命周期、栈钩子、手柄输入） |
| | AppStack | `main/app/appStack.hpp` | 单栈导航（push/pop/replace + 异步删除） |
| | AppStackManager | `main/app/appStackManager.hpp` | **系统级单例**多栈管理器（根栈 + 游戏独立栈 + 前台切换） |
| **AI Agent** | AiClient / AiParser / AiExecutor / AiSystemInfo | `main/aiAgent/` | OpenAI API 调用、JSON 解析、动作执行、系统信息采集 |
| | AiAgentOverlay | `main/app/aiAgentApp/` | 系统层浮动窗（`lv_layer_sys`），对话历史 + 长期记忆 |
| | PinyinDict | `main/app/aiAgentApp/pinyinDict.hpp` | 输入法词库加载（拼音/五笔/混合），FAT 文本 → `lv_ime_pinyin_set_dict` |
| | App (基类) | `main/app/app.hpp` | `getAiContext()` / `onAiSelect()` / `onAiActivate()` + 可选 `executeAiAction()` |
| **Lua 引擎** | LuaRuntime | `main/lua/luaRuntime.hpp` | Lua 5.5 VM RAII 封装，loadScript/loadString/callFunction |
| | luaDisplay | `main/lua/bindings/luaDisplay.cpp` | 简化绘图 API（fill_rect/draw_text/draw_circle 等），**LVGL 对象池化 + 静态跳过渲染** |
| | luaInput | `main/lua/bindings/luaInput.cpp` | 手柄输入轮询 API（is_pressed/joystick/connected） |
| | luaAudio | `main/lua/bindings/luaAudio.cpp` | 音频播放 API（play_sfx/play_bgm/set_volume） |
| | luaThread | `main/lua/bindings/luaThread.cpp` | 线程/延时 API（sleep/now/random） |
| | luaStorage | `main/lua/bindings/luaStorage.cpp` | 安全存档 API（save/load/remove/exists，限定 `/root/lua/data/`） |
| | luaTouch | `main/lua/bindings/luaTouch.cpp` | 多点触控轮询 API（is_down/x/y/is_just_pressed/is_just_released/all） |
| | LuaGameApp | `main/app/luaGameApp/` | Lua 游戏运行 App，C++ 驱动 60fps 循环调用 Lua loop(dt)，LVGL 对象池化渲染 |
| | LuaManagerApp | `main/app/luaManagerApp/` | Lua 脚本管理器（浏览/运行/删除/重命名，AI 生成入口；市场游戏按游戏 ID 显示、禁用重命名、删除=递归删目录） |
| **市场** | MarketClient / MarketConfig | `main/market/` | 游戏市场 HTTP 客户端（异步 GET 列表/内容/图片/manifest + POST 统计；条目含 type=lua\|elf） |
| **应用** | DesktopApp | `main/app/desktopApp/` | 桌面启动器，**动态卡片**（ELF 插件在前 + 系统卡片），3 焦点组导航 |
| | MarketApp | `main/app/marketApp/` | 游戏市场：Lua+ELF 混合截图卡片网格 + 详情 + 下载/更新/卸载（ELF 装到 `/root/elf/`） |
| | BleSettingsApp | `main/app/bleSettingsApp/` | BLE 手柄配对/管理 UI |
| | WifiSettingsApp | `main/app/wifiSettingsApp/` | WiFi 扫描/连接 UI |
| | TimeSettingsApp | `main/app/timeSettingsApp/` | 时间/SNTP 设置 UI |
| | PowerManagementApp | `main/app/powerManagementApp/` | 电源管理（电量、关机 Deep-sleep） |
| | TestApp | `main/app/testApp/` | 测试用 App |
| **跨主机联机** | HostManager | `main/host/hostManager.hpp` | 角色管理（主机/从机）、4 槽位管理、WsClient 连接池、mask 下发 |
| | wsConsoleHandler | `main/host/wsConsoleHandler.cpp` | `/ws/console` 管理通道（注册/全量 slots_update 广播/心跳） |
| | wsInputHandler | `main/host/wsInputHandler.cpp` | `/ws/input` WebSocket 入口（Web 客户端用）→ VirtualGamepad |
| | udpInput | `main/host/udpInput.cpp` | UDP 端口 8082 监听 + 发送（P4→P4 低延迟输入转发） |
| | WsClient | `main/websocket/wsClient.hpp` | 通用 WebSocket 客户端封装，`sendText` / `sendBinary` |
| | ConsoleManagerApp | `main/app/consoleManager/` | 多主机管理 UI（BLE 配对风格，4 槽位 + mask 控制 + 交换） |
| | **tetrisTcpHost** | `elfapps/tetris/main/app/tetris/net/tetrisTcpHost.hpp` | Tetris TCP 游戏服务器（端口 8083），异步 send Task 20fps，主机→从机 snapshot |
| | **TetrisRemoteClient** | `elfapps/tetris/main/app/tetris/net/tetrisRemoteClient.hpp` | Tetris 远程客户端，listen 端口 8084 等待主机连接，recv Task 解析 snapshot |
| | **TetrisRemoteApp** | `elfapps/tetris/main/app/tetris/tetrisRemoteApp.hpp` | Tetris 远程游戏 App，全屏渲染远程主机画面，多玩家动态分屏 |
| **ELF 插件层** | ElfManager | `main/elfManager/` | 插件模块管理（path→Module map，refcount + size/mtime 热更新，内部 RAM 栈 dlopen） |
| | elfSdk | `main/elfSdk/` | ABI 契约（`elf_plugin_info_t` v4 + `ELF_SDK_VERSION`）+ 符号表 `g_main_elfsyms`（脚本生成）+ 符号哈希解析 + 启动日志静默 |
| | elfapps | `elfapps/<name>/` | 5 个游戏 .so 插件工程（snake / chineseChess / fruitNinja / tetris / douDiZhu） |
| **网络** | WiFi | `main/wifi/wifi.hpp` | C 风格 API：STA/AP 双模 + NAT，`esp_wifi_remote` C6 通信 |
| | C6 Slave | `main/wifi/slave.hpp` | SDIO/ESP-HOSTED 协处理器管理器，后台 Task 早期启动 |
| | SocketStream | `main/wifi/socketStream.hpp` | TCP 输入/输出/双向流封装（`ISocketStream` / `OSocketStream` / `IOSocketStream`） |
| | SocketStreamWindow | `main/wifi/socketStreamWindow.hpp` | 连接池管理器（HTTP 6 worker 线程） |
| | HTTP Server | `main/server/` | 裸 socket TCP HTTP，端口 80，6 工作者线程，内置 HTML |
| | WS Server | `main/wsServer/` | WebSocket 端口 8080，动态 URI 注册/注销，广播 |
| | mDNS | `main/wifi/mdns.hpp` | mDNS 服务发现（`_http` 80, `_ws` 8080, `_p4console` 80） |
| | Discovery | `main/discovery/` | mDNS 设备发现（查询 `_p4console._tcp` 枚举其他主机） |
| | HTTP | `main/wifi/http.hpp` | HTTP 协议解析（Method/Status/Header 等） |
| **OTA** | OtaManager | `main/ota/otaManager.hpp` | 双槽 OTA 空中升级（**单例类**）：`POST /api/ota` 流式写入 + `GET /api/otaInfo` 状态查询 + bootloader 自动回滚检测（NVS 记录）+ 版本防重烧 |
| **媒体** | ScreenStream | `main/screenStream/` | MJPEG 硬件 JPEG 编码 + WebSocket 流，直接读 bridge 帧缓冲 |
| | ES8311 | `main/audio/ES8311.hpp` | ES8311 编解码器驱动（I²C + I²S） |
| | Audio | `main/audio/Audio.hpp` | 音频管理器（Meyer's Singleton）+ AudioHandle 值类型 |
| **外设** | BLE Gamepad | `main/bleGamepad/` | NimBLE + esp_hosted，4 玩家上限，NVS 持久化配对 |
| | 手柄固件 | [`gamepad/`](https://codeberg.org/loouwiit/esp32c6HidJoystick) (submodule) | ESP32-C6 BLE HID 手柄固件，独立仓库 |
| | Battery Manager | `main/battery/` | ADC 电池电量估算 + 充电检测 (GPIO23) + 充电动画 |
| | **BatteryMonitor** | `main/battery/batteryMonitor.hpp` | 低电量检测提醒（主机+手柄）：15s 轮询单阈值 → `lv_layer_sys` 弹窗 + 循环提示音，弹窗显示时屏蔽底层 App 输入 |
| | **SystemMonitor** | `main/monitor/` | 系统监视器：CPU 利用率 + 内存（内部 RAM/PSRAM 已用）+ 显示 FPS，串口输出 |
| **GUI** | GUI | `main/gui/` | 色板（8 色）+ 16 个工厂方法 (`createPage/Card/Flex/Button/Title/Switch/Slider` 等) |
| **存储** | FAT | `main/storage/fat.hpp` | FAT 分区 (16MB)，VFS `/root/`，文件/目录 CRUD |
| | memFS | `main/storage/mem.hpp` | 内存文件系统 (6MB max)，VFS `/root/mem` |
| | SD | `main/storage/sd.hpp` | SD 卡，VFS `/root/sd` |
| **补丁** | Patches | `patches/` | 3 个 patch（帧回调 / IME 修复合集 / PNG 禁用）+ PPA 官方补丁引导 NOTES，跨平台 `apply.py` 管理 |

---

## Lua 引擎模块

**模块路径**: `main/lua/`

### LuaRuntime — Lua VM RAII 封装

```cpp
class LuaRuntime {
    LuaRuntime();
    ~LuaRuntime();                          // lua_close()
    bool init();                            // 打开安全标准库（沙箱）
    bool loadScript(const char* path);      // luaL_loadfile + lua_pcall
    bool loadString(const char* code);      // luaL_loadstring + lua_pcall
    bool callFunction(const char* name, int nargs=0, int nresults=0);
    bool hasFunction(const char* name);
    const std::string& getLastError() const; // 获取上次错误
    lua_State* state();
};
```

### Lua 绑定模块

所有绑定在 `main/lua/bindings/` 下，提供简化高层 API：

| 模块 | 文件 | Lua API |
|------|------|---------|
| luaDisplay | `bindings/luaDisplay.cpp` | `fill_screen/fill_rect/draw_rect/draw_text/draw_line/draw_circle/width/height`（LVGL 对象池化 + 静态跳过） |
| luaInput | `bindings/luaInput.cpp` | `is_pressed/is_just_pressed/joystick/buttons/connected/player_count` |
| luaAudio | `bindings/luaAudio.cpp` | `play/play_sfx/play_bgm/stop_bgm/set_volume` |
| luaThread | `bindings/luaThread.cpp` | `sleep/now/random` |
| luaStorage | `bindings/luaStorage.cpp` | `save/load/remove/exists`（安全存档，限定 `/root/lua/data/`） |
| luaTouch | `bindings/luaTouch.cpp` | `count/is_down/x/y/is_just_pressed/is_just_released/get/all`（多点触控轮询） |

颜色格式：支持 `"#ff0000"` / `0xff0000` / `{r=255,g=0,b=0}` 三种。

**触摸输入**（`luaTouch`）：多点触控轮询，数据源为 `GT911Indev::getSlots()`（只读，**驱动层零改动**）。
`LuaGameApp::updateTouchSnapshot()` 每帧在 `loop(dt)` 前（无锁阶段）先归档 `prevDown=down` 再刷新当前槽位快照；
边沿在绑定内计算：`is_just_pressed(i) = down[i] && !prevDown[i]`（自上一帧以来的上升沿），状态与边沿同源自洽。
坐标与 `display` 模块一致（旋转后 1280×720 屏幕坐标）；槽位为虚拟手指（可被不同物理手指复用），逐槽轮询即可。

**手柄输入**（`luaInput`）：同样采用**帧快照模式**（与触摸同款，防边沿丢失竞态）。
- `VirtualGamepad::dispatchTask`（~60fps 独立 Task）经 `LuaGameApp::onGamepadInput` **并发写** `m_inputs[]`（实时缓冲）
- `gameLoop()` 每帧在游戏循环 Task 内两步：`memcpy(m_prevInputs, m_frameInputs)` 归档上一帧快照 → `memcpy(m_frameInputs, m_inputs)` 刷新当前帧快照
- Lua 绑定 `states/prevStates` 指向 `m_frameInputs`/`m_prevInputs`，边沿 `is_just_pressed = cur && !prev` 同源自洽
- ⚠ 修复前直接 `memcpy(m_prevInputs, m_inputs)`：按键上升沿若在归档前已被 dispatch Task 写入 → `prev` 已按下 → 整个按键期间 `cur && !prev` 恒 false，**本次按下被吸收永不触发**（A 键切色/B 键退出偶发失效的根因）。帧快照模式下最坏延迟 1 帧捕获，边沿不丢失
- **系统浮层门控**（AI Overlay `isVisible()` **或** 低电量弹窗 `BatteryMonitor::isAlertShown()`）：`gameLoop()` 每帧先判 `overlayVisible`，浮层可见时**手柄+触摸快照全部置中**（`GamepadState{}` 值构造 + `down/prevDown` 清空 + `downCount=0`，含清 `m_inputs` 实时缓冲防浮层关闭后残留按压泄漏），跳过采样——与 `VirtualGamepad::dispatchTask` 门控语义一致。⚠ 触摸快照直读 `GT911Indev::getSlots()` 是**原始输入**，不经 dispatch（dispatch 只拦手柄分发），浮层上触摸/按键若不在此拦截会穿透进 Lua 游戏（AI 键盘打字误触游戏）；⚠ 置中必须用 `GamepadState{}` 值构造（dpad=15 松开/摇杆 128），`memset(0)` 会把 dpad 置 0=「上」按下=伪造输入
⚠ `LuaGameApp::init()` 的 `m_canvas` 须移除 `LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE`（`lv_obj_create` 默认可滚动+可点击，子对象越出画布边界时触摸拖拽会滚动整个画布——FruitNinja 画布同款处理；`LV_SCROLLBAR_MODE_OFF` 只隐藏滚动条，不防滚动）。

### Lua 沙箱（安全边界）

`LuaRuntime::init()` 只打开**安全标准库**（base/coroutine/math/string/table/utf8），并移除 base 里的 `dofile`/`loadfile`；**io / os / package / debug 一律不开放**。所有 Lua 脚本（AI 生成 + 市场下载）统一沙箱化。

- 文件读写只能通过 `storage` 模块（白名单 `/root/lua/data/`，禁止路径穿越，单文件 ≤256KB）
- 防死循环：`LuaGameApp` 每帧调用 `loop(dt)` 前挂指令计数 hook（`lua_sethook`，1000 万条/帧上限），超限中断并走 `reportLuaError()` 错误路径
- 效果：脚本无法读取系统文件（如 AI API Key）、无法删改系统数据

### LuaGameApp — Lua 游戏运行器

**模块路径**: `main/app/luaGameApp/`

```cpp
class LuaGameApp : public App {
    LuaGameApp(Display* display, const char* scriptPath);
    LuaGameApp(Display* display, const char* scriptCode, size_t codeLen);
    static LuaGameApp* createFromAIGenerated(Display* display,
                                              const std::string& name,
                                              const std::string& code);
};
```

**游戏循环契约**（Lua 脚本必须实现）：
```lua
function setup() end                     -- 初始化（可选）
function loop(dt) return true end        -- 每帧调用，返回 false 退出
```

C++ 驱动：独立 Task @60fps → 持 LVGL 锁 → 更新输入 → 调 Lua `loop(dt)` → 释放锁 → `vTaskDelayUntil`

#### 渲染性能设计（对象池化）

**根因**：P4 的 PSRAM 有效带宽极低（~35-40MB/s 实测），FPS 与每帧 PSRAM 流量成反比。早期 canvas 整缓冲方案每帧做全缓冲操作（fill_screen 1.76MB + 整屏失效 blit ≈ 3.5MB）→ 仅 10fps；再叠加双缓冲 + 块 diff（≈ 7MB/帧）→ 6fps。**任何全缓冲操作在 P4 上都是性能杀手**。

**现架构**：`luaDisplay` 绑定改用 **LVGL 对象池化 + 静态跳过**，回归 LVGL 原生合成，**零整缓冲操作**：

- **槽位对象池**（`LuaDisplayContext::Slot slots[64]`）：按**帧内绘制顺序**对齐复用 LVGL 对象（不创建/销毁）。`slotAcquire(kind,x,y,w,h,color,text)` 取槽位。
- **静态跳过**：参数与上帧完全相同 → 返回 `nullptr`，不更新不失效（地面/云/标题只画一次）；移动对象（管道/小鸟/分数）才 `set_pos`/`set_text` → LVGL 只重绘新旧两个小区域。
- **fill_screen 用容器背景样式**，颜色不变即跳过（不逐像素填充）。
- **文本用 `lv_label`**（LVGL freetype 原生渲染），静态文本跳过，无需 glyph 缓存。
- **kind 变化重建**：槽位 shape↔text 切换时删旧建新（不同 widget 类型不可复用）。
- **槽位回收**：`luaDisplayBeginFrame` 清理上一帧多出的槽位（绘制数减少时），重置槽位索引。
- paint 持锁（gameLoop `lockGuard` 包 `loop(dt)`），每帧只更新少量移动对象 → 锁窗口极小。

**槽位漂移**：Lua 用 `table.insert/remove` 管理对象列表时，中间增删使后续槽位串位——对象池按槽位直接更新即可（正确，仅当帧多更新几个对象），下一帧静态跳过恢复。

**性能实测**：Flappy Bird 平静 ~60fps、极限（大量管道）~20fps，CPU 总量大幅下降。

⚠ 依赖**每帧绘制顺序稳定**（Lua 游戏均固定顺序重画）；脚本绘制顺序/数量乱序时退化为全量更新（结果正确，仅无静态跳过收益）。

### AI 集成动作

| 动作 | 说明 | 示例 |
|------|------|------|
| `generate_lua_game` | AI 生成 Lua 游戏并运行 | `{"action":"generate_lua_game","text":"贪吃蛇","params":{"code":"..."}}` |
| `read` | AI 读取 FAT 文件内容（配 `nextRound:true`） | `{"action":"read","text":"/root/lua/game/test.lua","nextRound":true}` |

**错误回传**：Lua 运行时错误自动存入 `AiAgentOverlay::m_pendingLuaError`，下次对话注入 system prompt，AI 可修复后重新生成。`read` 读取的文件内容通过 `m_pendingFileRead` 同样注入 system prompt。两者均在 `onSend()` 和 `nextRound` follow-up 双路径中注入。

---

## 基础设施层

### Task — 协程调度器

**模块路径**: `main/task/`

简单的轮询式协程调度器，每个 `Task` 包含一个函数指针和定时参数，在 daemon 线程中循环调用。

```cpp
class Task {
public:
    // ── 优先级常量 ──
    class Priority {
        static constexpr UBaseType_t Deamon  = 2;
        static constexpr UBaseType_t Normal  = 5;
        static constexpr UBaseType_t High    = 6;
        static constexpr UBaseType_t RealTime = 8;
    };

    // ── CPU 亲和性 ──
    class Affinity {
        static constexpr size_t None = -1;        // 无限制
        static constexpr size_t NotAssigned = -2; // 未指定
    };

    // 函数返回下次唤醒延迟（ms），infinityTime = portMAX_DELAY
    using Function_t = TickType_t(*)(void* param);

    static void init(size_t daemonThreadCount);
    static Task* addTask(Function_t func, const char* name,
                         void* param = nullptr,
                         TickType_t callTick = 0,
                         Affinity affinity = NotAssigned);
    static void removeTask(Task* task);
    static void setAffinity(Task* task, Affinity affinity);
};
```

**使用方式**:
```cpp
Task::init(2);  // 2 个 daemon 线程

// 注册一个每秒执行的任务（示例：周期性采样可参考 SystemMonitor）
Task::addTask([](void*)->TickType_t {
    ESP_LOGI(TAG, "tick");
    return 1000;
}, "demo", nullptr, 1000, Task::Affinity::None);
```

### Mutex — RAII 互斥锁

**模块路径**: `main/mutex/`

```cpp
class Mutex {
    Mutex();               // 构造 xSemaphoreCreateMutex
    ~Mutex();              // 确保析构前无人持有
    bool try_lock() const;
    void lock() const;
    void unlock() const;
};

class Lock {
    Lock(const Mutex& mutex);   // 构造时 lock
    ~Lock();                    // 析构时 unlock
};
```

**⚠ 不可拷贝/移动（编译期拦截）**: `Mutex` / `RecursiveMutex` 的拷贝/移动构造与赋值均已 `= delete`。
互斥锁句柄按值拷贝 + 临时对象析构 `vSemaphoreDelete` 会留下**悬空句柄** → 后续 `lock()`
在已释放队列上 assert 崩溃（`pxQueue->uxItemSize == 0`，曾致 App 切换偶发卡死）。
因此含锁成员的对象（如 `AppStackManager`）**必须用单例/常驻存储，不可 `std::move` 赋值**。

**与 Display::LockGuard 的区别**:
- `Mutex` / `Lock` — 用于 BleGamepad 等模块内部的多线程保护，**非递归**互斥锁
- `Display::LockGuard` — LVGL 内部**递归互斥锁** (`xSemaphoreCreateRecursiveMutex`)，同一 task 可嵌套加锁

### GPIO — 引脚封装

**模块路径**: `main/gpio/`

```cpp
class GPIO {
    constexpr static GPIO_NUM NC = GPIO_NUM_NC;

    GPIO(GPIO_NUM gpio);
    GPIO(GPIO_NUM gpio, Mode mode);
    GPIO(GPIO_NUM gpio, Mode mode, Pull pull, Interrupt intr,
         function_t func = nullptr, void* param = nullptr);

    GPIO& operator=(bool level);  // 写电平
    operator bool();              // 读电平
    operator GPIO_NUM();          // 取引脚号
    operator int();

    void setMode(Mode mode);
    void setInterrupt(Interrupt intr, function_t func, void* param);
    static void enableGlobalInterrupt();
};
```

### IIC — I²C 总线

**模块路径**: `main/iic/`

```cpp
class IIC {
    IIC(GPIO clock, GPIO data, IicPort port = IicPortAuto);
    ~IIC();
    bool detect(uint16_t address);          // 检测设备是否存在
    i2c_master_bus_handle_t getBusHandle();
};

class IICDevice {
    IICDevice(IIC& iic, uint16_t address, unsigned speed = 100000);
    bool transmit(const void* buffer, size_t size);
    bool receive(void* buffer, size_t size);
    bool request(const void* write, size_t writeSize, void* read, size_t readSize);
};
```

ES8311 编解码器、GT911 触摸均通过 IIC 通信。

### VirtualGamepad — 统一手柄输入抽象层

**模块路径**: `main/gamepad/virtualGamepad.hpp`

系统层单例，汇集所有手柄输入源（本地 BLE + 远程客户端），
在专用 Dispatch Task 中以固定 60fps 全量分发到活跃 App。

```cpp
class VirtualGamepad {
public:
    static VirtualGamepad& instance();
    void start(Display* display);
    /// 统一注入（hostId: 0=本机, 1-3=远程设备。按掩码过滤后 dispatchTask 分发）
    void inject(int8_t hostId, int8_t playerId, const GamepadState& state);
    void resetPlayer(int8_t hostId, int8_t playerId);
    void setHostMask(int8_t hostId, uint8_t playerMask);
    void setAllMasks(const uint8_t masks[4]);

    // ── 远程转发（从机模式，UDP）──
    void startForwarder();    // 启动独立 Thread 通过 UDP 转发本机输入
    void stopForwarder();

    /// 等待当前分发结束（拿即放）：语义 = 确保此刻无 dispatchTask 分发进行中。
    /// deleteApp 删除 App 前调用，避免 dispatchTask 在已删除对象上调用虚函数（UAF）。
    /// ⚠ 必须在未持有 LVGL 锁 / AppStackManager::m_mutex 时调用（锁序：dispatchLock 在前）
    void waitDispatchIdle();
};
```

**架构**:
```
BLE Task  → inject(0, p, state)     ← 本机
UDP Task  → inject(hostId, p, state) ← 远程（来自 UDP 端口 8082）
WS Task   → inject(hostId, p, state) ← 远程（来自 /ws/input，Web 客户端）
                                              ↓
VirtualGamepad:
  ├── m_states[hostId][playerId] = state  (存储)
  ├── forwarderThread @60fps UDP → 远程主机端口 8082（从机模式）
  └── dispatchTask() @60fps → app->onGamepadInput(hostId, playerId, state)
        └── 分发段持 m_dispatchLock（见下「分发互斥锁」）
```

| 特性 | 说明 |
|------|------|
| inject(hostId, ...) | hostId=0 本机，1-3 远程；仅存状态，不修改 mask |
| 分发频率 | 固定 60fps (16ms)，独立 Dispatch Task |
| 全量更新 | 每次遍历 `m_hostMask` 启用的 (hostId, playerId) |
| 掩码默认 | `{0x0F, 0x00, 0x00, 0x00}`（仅本机 P0-P3 启用；远程输入默认不进入本地游戏，由 ConsoleManager 打开后 `setAllMasks` 按用户配置覆盖） |
| 掩码来源 | 主机由 ConsoleManager UI 控制；从机接收主机下发的反码 |
| AI Overlay | `isVisible()` 时跳过分发，AI 独占输入 |
| 系统浮层屏蔽 | AI Overlay `isVisible()` **或** 低电量弹窗 `BatteryMonitor::isAlertShown()` 时跳过分发，底层 App 不收输入 |
| 从机转发 | forwarderThread 后检查 `~m_hostMask[0] & 0x0F` 决定转发哪些玩家到主机 |
| 传输方式 | UDP（11 字节/帧），持久 socket `connect()` + `send()`，零创建开销 |
| 注入去重 | 状态未变化时不转发（forwarder 侧有 `m_lastForwarded` 去重） |

**分发互斥锁（防 UAF）**：dispatchTask 分发段（门控检查 + `getActiveApp()` + handler 循环）持 `Mutex m_dispatchLock`；`AppStack::scheduleDeletion` 的 deleteApp 任务在 `delete` 前调用 `waitDispatchIdle()`（拿即放）确保无分发进行中 → **不会在已删除 App 上调用 `onGamepadInput` 虚函数**（崩溃根因：vGamepadDisp 悬空指针，deleteApp 双核并发恰在帧内完成 delete）。
- ⚠ **锁序 `dispatchLock → LVGL`**：handler 内等 LVGL 锁，deleteApp 侧同样先 `waitDispatchIdle()`（不持锁等待）再 `lockGuard`——两者一致无 ABBA 死锁；**严禁**先持 LVGL 再等 dispatchLock
- ⚠ 锁只包分发段，**不包 `vTaskDelay(16)`**：否则 deleteApp 会被迫等整帧，无意义拉长删除延迟
- 确定性保证：`applyApp`（替换 activeApp）总是先于 `scheduleDeletion`（pop/replace/空栈三分支均如此）→ deleteApp 拿到锁后，dispatchTask 下一帧必拿新指针
- 范围：仅保护 dispatchTask 分发路径；`AiExecutor`/`AiAgentOverlay` 的 `getActiveApp()` 裸读不走此锁（独立机制，未覆盖）

**`onGamepadInput` 签名**:
```cpp
// hostId: 0=本机, 1-3=从机；playerId: 设备上的手柄编号 (0-3)
virtual void onGamepadInput(int8_t hostId, int8_t playerId, const GamepadState& state);
```

**掩码语义**（由 ConsoleManager 控制，wsConsoleHandler 广播到从机）：
- **主机侧**: `m_hostMask[hostId]` 的 bit[playerId] = 1 → 该输入进入本地游戏
- **从机侧**: 收到主机 mask 后存为**反码**，`m_hostMask[0]` bit=1 → 留在本地处理；bit=0 → forwarder 取反后转发到主机
- 同一手柄输入不会同时被本地和远程消费（mask 互斥）

**集成点**:
- BleGamepad::processTask → `inject(0, playerId, state)` 代替直接调 App
- BleGamepad::start 末尾 → `VirtualGamepad::instance().start(display)`
- BLE_GAP_EVENT_DISCONNECT → `resetPlayer(0, playerId)`
- ConsoleManager::applyMaskToVirtualGamepad → 触发 `wsConsoleBroadcastSlotsUpdate`

### `/ws/input` — Web 客户端远程手柄输入通道

**模块路径**: `main/host/wsInputHandler.cpp`

接收 11 字节二进制帧（来自 Web 客户端 gamepad.html），解析后注入 VirtualGamepad。

**协议格式**:
| 偏移 | 大小 | 字段 |
|------|------|------|
| 0 | 1 | `hostId` — 设备号 (0=本机保留, 1-3=远程) |
| 1 | 1 | `localPlayer` — 设备上的手柄编号 (0-3) |
| 2-3 | 2 | `buttons` — uint16 小端序 |
| 4 | 1 | `lx` |
| 5 | 1 | `ly` |
| 6 | 1 | `rx` |
| 7 | 1 | `ry` |
| 8 | 1 | `lt` |
| 9 | 1 | `rt` |
| 10 | 1 | `dpad` |

- fd → hostId 追踪，断开时 `resetPlayer(hostId, *)` 清理
- P4→P4 联机使用 UDP（端口 8082），不经过此通道

### UDP Input — P4→P4 低延迟输入转发

**模块路径**: `main/host/udpInput.cpp`

两台 P4 设备间的手柄输入转发。发送方在 `forwarderThread` 中通过持久 UDP socket 发送 11 字节帧，
接收方在后台 Task 中监听 UDP 端口 8082。

```cpp
// 发送方（从机 forwarderThread 自动调用）
int sock = udpInputCreateSocket(host, 8082);   // 有 connect()
udpInputSend(sock, buf, 11);                   // 用 send()，无连接创建开销
udpInputCloseSocket(sock);

// 接收方（udpInputStart() 在 main.cpp 中启动）
// 后台 Task 监听端口 8082，recvfrom → VirtualGamepad::inject()
```

**协议格式**：与 `/ws/input` 相同（11 字节二进制帧）

| 特性 | 说明 |
|------|------|
| 传输方式 | UDP，每个 datagram 原子到达，无 Nagle 缓冲 |
| 丢包容忍 | 输入帧 60fps，丢一帧不影响后续 |
| Socket 管理 | 持久 socket，`connect()` + `send()`，零 TIME_WAIT |
| 接收方式 | 独立 Task，`recvfrom` 5 秒超时，自动清理断连 source |
| 端口 | 8082（不同于 WS 的 8080）|

### HostManager — 跨主机角色与连接管理

**模块路径**: `main/host/hostManager.hpp`

管理当前设备的角色（主机/从机）、4 设备槽位、WsClient 连接池、mask 下发。

**角色模型**：

| 角色 | 默认 | 触发 |
|------|------|------|
| 主机 (Host) | ✅ | 从未连接他人 |
| 从机 (Client) | ❌ | 用户从 ConsoleManager 点击连接远程主机，**放弃自身主机资格** |

**槽位**（Slot 0=本机，Slot 1-3=从机）：

```
┌───┬───┬───┬───┐
│ 0 │ 1 │ 2 │ 3 │
│本机│ B │ --│ --│
└───┴───┴───┴───┘
```

**从机连接流程**：
```
从机→主机:  WS connect /ws/console
从机→主机:  {"type":"register","hostname":"esp32p4-b","gamepad_count":2}
主机→从机:  {"type":"registered","slot":1}
主机→所有:  slots_update（全量槽位 + mask 广播）
```

**API**：
```cpp
class HostManager {
    static HostManager& instance();

    // ── 角色 ──
    bool isHost() const;
    bool isClient() const;
    int  getMySlot() const;

    // ── 从机侧 ──
    bool connectToHost(const char* ip, uint16_t port = 8080, const char* hostname = nullptr);
    void disconnectFromHost();
    bool isConnectedToHost() const;
    const char* getRemoteHostname() const;
    WsClient* getWsConnection(const char* uri);   // 连接池，同 URI 复用

    // ── 主机侧 ──
    struct ClientSlot {
        int slotId; char hostname[64]; char ip[16];
        int gamepadCount; bool connected;
    };
    const ClientSlot* getSlots() const;

    // ── 主机 mask ──
    void setHostMask(const uint8_t mask[4]);
    const uint8_t* getHostMask() const;

    // ── 事件回调 ──
    void setOnSlotUpdate(std::function<void(const ClientSlot* slots)> cb);
    void setOnRoleChange(std::function<void(bool isClient)> cb);

    // ── 内部（由 wsConsoleHandler 调用）──
    void onClientRegistered(int slotId, const char* hostname, const char* ip, int gamepadCount);
    void onClientDisconnected(int slotId);
    void swapSlots(int slotA, int slotB);  // 同时交换 mask
};
```

### `/ws/console` — 设备管理通道

**模块路径**: `main/host/wsConsoleHandler.cpp`

从机注册到主机的管理通道，独立于游戏数据通道。**所有状态变更使用全量 `slots_update` 广播**（不增量）。

| 消息 | 方向 | 说明 |
|------|------|------|
| `{"type":"register","hostname":"...","gamepad_count":N}` | 从机→主机 | 注册带手柄数 |
| `{"type":"registered","slot":N}` | 主机→从机 | 分配槽位 |
| `{"type":"slots_update","slots":[...]}` | 主机→所有 | **全量**槽位+mask 广播（替代增量消息） |
| `{"type":"ping"}/{"type":"pong"}` | 双向 | 心跳 |

**`slots_update` 格式**（每槽带 `mask` 字段）：
```json
{"type":"slots_update","slots":[
  {"slot":0,"hostname":"esp32p4-a","connected":true,"gamepad_count":0,"mask":15},
  {"slot":1,"hostname":"esp32p4-b","connected":true,"gamepad_count":2,"mask":3},
  {"slot":2,"hostname":"","connected":false,"gamepad_count":0,"mask":0},
  {"slot":3,"hostname":"","connected":false,"gamepad_count":0,"mask":0}
]}
```

触发时机：register / disconnect / swap / mask 变化 → 全量广播一次。

### InputHost — 远程设备连接跟踪

**模块路径**: `main/host/inputHost.hpp`

3 槽位 fd→hostId 映射表，供 `/ws/console` 管理通道使用（后续设计）。
当前由 `/ws/input` handler 用于断开清理。

```cpp
class InputHost {
    static InputHost& instance();
    int  registerClient(int fd, const char* hostname);  // 返回 hostId 1-3, -1 满
    void unregisterClient(int fd);
    int  getHostIdByFd(int fd) const;
};
```

### WsClient — 通用 WebSocket 客户端

**模块路径**: `main/websocket/wsClient.hpp`

薄封装 `esp_websocket_client`，供从机连接远程主机。

```cpp
class WsClient {
    bool connect(const char* host, uint16_t port, const char* uri);
    void disconnect();
    bool isConnected();
    bool sendText(const char* data, size_t len);
    bool sendBinary(const uint8_t* data, size_t len);
    void setOnMessage(std::function<void(const char*, size_t)> cb);
    void setOnConnected(std::function<void()> cb);
    void setOnDisconnected(std::function<void()> cb);
};
```

---

### TimeService — 全局时间同步服务

**模块路径**: `main/time/timeService.hpp` / `timeService.cpp`

系统级单例（`TimeService::instance()`），在 `main.cpp` 的 `wifiStart()` 后 `start()` 一次性初始化 SNTP 并保持运行。

```cpp
class TimeService {
    static TimeService& instance();

    void start();                                   // 初始化 SNTP（仅一次）+ 读 NVS 自动开关 + 注册 GOT_IP 事件
    bool isAutoSync() const;                        // 自动同步开关（NVS 持久化，默认开启）
    void setAutoSync(bool enable);                  // 设置并持久化；开启且已连 WiFi 时立即同步
    void forceSync();                               // 立即重启 SNTP 重新同步（幂等）
    bool isSynced() const;                          // 系统时间是否已同步（tm_year > 70）
    time_t getLastSyncUnix() const;                 // 最近一次成功同步的 Unix 时间

    using SyncCbHandle = uint32_t;
    SyncCbHandle addSyncCb(std::function<void()> cb); // 注册同步完成回调，返回句柄
    void removeSyncCb(SyncCbHandle handle);           // 按句柄定点清除（幂等）
};
```

**行为**：
- **自动同步默认开启 + NVS 持久化**：namespace `"time"` / key `"auto_sync"`，重启保持
- **WiFi 连接时同步**：监听 `IP_EVENT_STA_GOT_IP`，自动模式下调 `forceSync()`
- **周期性同步**：lwIP SNTP 内置轮询（`CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`，1h），服务持续运行即自动校正
- **同步完成通知**：`config.sync_cb`（⚠ ESP-IDF v5.5.2 **无** `NETIF_SNTP_TIME_SYNC` 事件，须用回调）

**多回调 + 定点清除（防悬垂）**：
- `addSyncCb` 支持多个监听者，返回句柄；`removeSyncCb(handle)` 精确移除
- 回调容器 `Mutex` 保护；`notifySyncDone()` **持锁直接调用**（不拷贝），与 `removeSyncCb`（App `deinit()` 调用）互斥
  → 回调执行时注册者必然存活，App 退出后事件线程不再访问已销毁对象
- 约定：回调体内不得再调用 TimeService 方法（防非递归锁死锁），应保持轻量

**⚠ 注意**：`esp_netif_sntp_init()` 只能成功调用一次（重复调用返回 `ESP_ERR_INVALID_STATE`）。
`forceSync()` 用 `esp_netif_sntp_start()`（已启动时自动 stop+init 重启），切勿重复 init。

**接入 App**（`TimeSettingsApp`）：`init()` 从 `isAutoSync()` 同步开关显示 + `addSyncCb` 注册；`deinit()` `removeSyncCb` 清除。

---

## 显示与输入

### Display — LVGL 适配层

**模块路径**: `main/display/display.hpp`

| 方法 | 说明 |
|------|------|
| `init()` | 初始化 LVGL 适配器 |
| `bindDisplay(panel, io, w, h, tearAvoid, rotation)` | 绑定 ILI9881c 面板 |
| `start()` | 启动 LVGL worker task |
| `lockGuard()` | RAII 递归锁获取（LVGL 线程安全） |
| `setBrightness(percent)` | 设置背光（委托到 ILI9881c） |
| `getBrightness()` | 读取当前背光 |
| `saveBrightness()` | 持久化背光到 NVS |
| `getLvglDisplay()` | 获取 `lv_display_t*` |
| `getActiveApp()` | 获取当前前台 App |
| `setStackManager(mgr)` | 绑定 AppStackManager |
| `getFps()` | 获取当前帧率 |

**LockGuard**:
```cpp
if (auto guard = display->lockGuard()) {
    lv_label_create(screen);
    // LVGL 操作...
}   // 析构自动 unlock
```

### ILI9881c — 面板驱动

**模块路径**: `main/display/ili9881c.cpp`

单例模式（`getInstance()`），封装 MIPI DSI 初始化序列（见 `initCommand.inl`）和背光 PWM 控制。

### FontLoader — 字体加载

**模块路径**: `main/display/font.cpp`

```cpp
class FontLoader {
public:
    enum class FontSize : uint16_t {
        Small  = 24,
        Medium = 32,    // Default
        Large  = 56,
    };

    static const lv_font_t* load(const char* vfsPath, uint16_t size);
    static const lv_font_t* getDefault(FontSize size = FontSize::Default);
    static bool setDefault(const lv_font_t* font, FontSize size = FontSize::Default);

private:
    struct FontEntry {
        FontSize size{};
        const lv_font_t* font{};
    };
    static constexpr size_t MaxCount = 3;
    EXT_RAM_BSS_ATTR static FontEntry s_fonts[MaxCount];
};
```

**VFS 驱动器 'F'**: 将 `"F:..."` 映射到 `/root/`，使 FreeType 通过 LVGL VFS 读取 FAT 分区文件。

**字体回退链**: NotoSC 缺少的符号（电池图标等）由 LVGL 内置 symbol 字体提供：
```cpp
const_cast<lv_font_t*>(FontLoader::getDefault(FontSize::Small))->fallback = lv_font_get_default();
```

### Touch — GT911

**模块路径**: `main/touch/`

| 特性 | 值 |
|------|-----|
| 接口 | I²C |
| 地址 | `0x5D` / `0x14` (备用) |
| 分辨率 | 720 × 1280 |
| 最大触控点 | 5 |
| INT/RST | GPIO46 / GPIO47 |

### GT911Indev — 五 indev 多指桥接层

**模块路径**: `main/touch/gt911Indev.hpp` / `gt911Indev.cpp`

单例（`GT911Indev::instance()`），把 GT911 硬件 5 点（含 track_id）转发到 **5 个独立 LVGL pointer indev**。LVGL 每 indev 是独立指针状态机 → 多指可同时按下、各自向光标下对象发 PRESSED/PRESSING/RELEASED，**所有 App 零改动**获得多指 LVGL 事件（`lv_indev_active()` 取各自坐标）。

```
GT911 INT(GPIO47) → ISR 给信号量 → 读线程 (Touch::update 读全 5 点)
   → track_id→槽位路由 → 5×Slot → indevReadCb(slot) → lv_indev#0..4 → LVGL 事件
```

| 特性 | 说明 |
|------|------|
| 中断驱动 | `esp_lcd_touch_register_interrupt_callback_with_data` + 二进制信号量；读线程 `take(sem, 30ms)`（INT 主触发 + 30ms 超时兜底保移动刷新），空闲零轮询 |
| track_id 路由 | `m_slotOfTrackId[256]`（track_id→槽，-1=未分配）+ `Slot[5]`，`Mutex` 保护读线程 ↔ read_cb |
| release 握手 | 槽 RELEASED 被 read_cb 消费前**不得**呈现新按压/坐标（`pendingPress` 暂存）→ 防 track_id 复用"未释放跳坐标"（PRESS_LOST/假点击）；消费后提升 PRESSED 或释放绑定 |
| 接线 | `main.cpp` 用 `GT911Indev::instance().start(&display, &touch)` 取代 `display.bindTouch()`（`display.start()` 之前） |
| 调试 | `getSlots()` 供 TestApp 多指可视化（5 圆点 + 槽号:trackId + 按下/释放态） |

```cpp
class GT911Indev {
    static GT911Indev& instance();
    bool start(Display* display, Touch* touch);
    void stop();
    struct SlotInfo { bool active; bool pressed; int8_t trackId; lv_point_t point; };
    void getSlots(SlotInfo* out, size_t max) const;
};
```

**已知取舍**：多指同时落在可滚动列表会各自触发滚动（LVGL 无"主指"概念）——本机 UI 以按钮/桌面为主，可接受。

### VirtualIndev — 虚拟触摸输入

**模块路径**: `main/virtualIndev/`

单例类，用于从 WebSocket/HTTP 注入触摸事件到 LVGL 输入设备系统。

```cpp
class VirtualIndev {
    static VirtualIndev& instance();
    bool start(Display* display);
    void sendTouch(lv_indev_state_t state, lv_point_t point);
};
```

- **无锁一写一读**：WebSocket handler 写 `sendTouch`，LVGL task 读 `indevReadCb`，单缓冲最新值覆盖
- `sendTouch()` 内置**服务端去重**：坐标+状态都没变且上条尚未消费时直接跳过
- `indevReadCb` 使用 **`hasNewData` + `continue_reading`**：
  - 有新事件 → 清标志 + `continue_reading=true` 让 LVGL 立即重读
  - 无新事件 → 维持当前按压状态 + `continue_reading=false` 空闲等待
- 由 `wsServer` 的 handler 调用 `sendTouch()` 注入触摸坐标

---

## 输入法与词库

AI 助手输入法基于 LVGL `lv_ime_pinyin`（K26 全键盘），支持**拼音 / 五笔 / 英文**三态循环切换（键盘 🔣 按钮），词库从 FAT 分区加载。

### 三态模式（`AiAgentOverlay::ImeMode`）

| 模式 | 词库 | 说明 |
|------|------|------|
| `Pinyin`（拼） | `pinyin_dict.txt` | 拼音输入，简体优先，常用字靠前 |
| `Wubi`（五） | `wubi_pinyin_dict.txt` | **五笔+拼音混合**：五笔字优先，拼音兜底 |
| `English`（EN） | — | 英文直接上屏 |

- 键盘 🔣（LV_SYMBOL_KEYBOARD/CLOSE）按钮循环切换：拼 → 五 → EN → 拼
- 切换时 `setImeMode()` 强制 `LV_IME_PINYIN_MODE_K26`（覆盖 LVGL 内部 🔣 触发的 K26/K9 翻转，K9 未编译）
- **空格上屏**：中文模式有候选时按空格上屏当前候选（无选中默认第一个），无候选放行空格
- 候选面板每页 6 字，`<`/`>` 或手柄翻页（每音候选不受 6 限制）

### 词库机制

```cpp
class PinyinDict {
    static const lv_pinyin_dict_t* loadFromFat();       // 拼音 /root/system/pinyin_dict.txt
    static const lv_pinyin_dict_t* loadWubiFromFat();   // 纯五笔（备用）
    static const lv_pinyin_dict_t* loadMixedFromFat();  // 五笔+拼音混合（"五"模式使用）
};
```

- 词库为 `lv_pinyin_dict_t { py, py_mb }` 数组（`{NULL,NULL}` 终止），`py_mb` 汉字 3 字节 UTF-8 直接连接
- **必须按字典序**（LVGL 前缀匹配返回第一个命中，短码在前）
- 加载：解析 FAT 文本 → qsort 字典序 → PSRAM 紧凑分配；失败回退 LVGL 内置词库
- `lv_ime_pinyin_set_dict()` 注入（创建 IME 后 / 切换模式时）
- 生成脚本在 `tools/`，源为 Rime（`~/.config/ibus/rime/`）：
  - `gen_pinyin_dict.py` → 从 `cn_dicts/8105.dict.yaml`（通用规范汉字表 8105 字 + 字频）提取
  - `gen_wubi_dict.py` → 从 `wubi86_jidian.dict.yaml` 提取单字
  - `gen_wubi_pinyin_dict.py` → 混合（五笔优先 + 拼音兜底去重）

### LVGL 必要 patch

集中在 `patches/0002-lvgl__lvgl-ime-pinyin-fixes.patch`（原 `0005`–`0010` 六个补丁的**合并版**，改同一文件 `lv_ime_pinyin.c`）：

- `0005`：移除 `pinyin_search_matching` 的 `i/u/v` 开头过滤（五笔 i 水 / u 立 / v 女折 键区）
- `0006`：`count` 从 `uint8_t`→`uint16_t`（五笔每组上千条目，uint8_t 截断 255 导致后段编码匹配失败）
- `0008`：候选选中状态管理——空槽设 `CTRL_HIDDEN`（占位保留、方向键/触摸导航自动跳过）；每次新拼音输入选中回第一个候选；翻页保留选中原位、仅落在空槽时回退第一候选；提交/清空后清除触摸 CHECKED 残留 + 选中重置；无匹配/空词条（如拼音 i）不显示空候选面板
- `0009`：候选选中高亮修复——① `lv_ime_pinyin_cand_panel_event` 改用事件参数路由按键（overlay 程序化翻页不再把选中移到 `<`/`>` 键上）；② `pinyin_page_proc` 无效翻页恢复原选中、有效翻页后选中不回落到翻页键/空槽（`sel<1 || sel>valid` 回退第一候选）；③ `pinyin_input_proc`/`pinyin_ime_clear_data` 清除键盘式 ENTER 残留的 `LV_STATE_PRESSED`（keypad 释放轮 `key=0` 导致 `LV_EVENT_RELEASED` 永不发出 → 对象 PRESSED 卡死 → 蓝色随机出现/消失）；配套 `aiAgentOverlay` 候选面板新增 `LV_PART_ITEMS|LV_STATE_FOCUSED` 蓝色样式（组焦点在候选面板时选中候选常显蓝色，替代依赖瞬时 PRESSED 的不稳定高亮）
- `0010`：翻页后焦点保持——修正 0009 的「选中不在当前页有效候选一律回退第一候选」：翻页键（`<`/`>`，id 0/7）原样保留，A 点 `>` 后焦点停在 `>` 上可连续翻页（与摇杆翻页不移动焦点一致）；仅当选中在候选槽且超出当前页有效数（新页候选变少）才回退第一候选；无效翻页同样恢复翻页前选中（含翻页键）
- 详见 [Patches](#patches) 章节

---

## LVGL 锁规则

`Display::lockGuard()` 获取 LVGL 内部互斥锁。该锁是**可重入（递归）互斥锁**（`xSemaphoreCreateRecursiveMutex`），同一 task 可多次调用 `lockGuard()` 嵌套加锁而不会死锁。

以下场景需注意：

| 调用上下文 | 能否执行栈操作 / LVGL 操作 | 处理方式 |
|-----------|--------------------------|---------|
| BLE 手柄回调 (`onGamepadInput`) | ✅ 可直接调 `pushApp`/`popApp` | BLE task 不持锁 |
| 普通 Task (`Task::addTask`) | ✅ 可直接调 | 独立 task |
| LVGL 事件回调 (`lv_event_cb`) | ⚠️ 部分情况须 `Task::addTask` 延后 | LVGL task 内部分操作会导致 LVGL 异常 |

---

## AppStack — 多栈导航系统

**模块路径**: `main/app/`

| 文件 | 内容 |
|------|------|
| `app.hpp` / `app.cpp` | `App` 基类 |
| `appStack.hpp` / `appStack.cpp` | `AppStack` 单栈 |
| `appStackManager.hpp` / `appStackManager.cpp` | `AppStackManager` 多栈管理器 |

### 单例（系统级，程序运行期常驻）

`AppStackManager` 为 **Meyer's Singleton**（仿 `OtaManager`），构造/析构私有：

```cpp
static AppStackManager& instance();   // magic static，仅构造一次 + 线程安全
void init(Display* display);          // main.cpp 单线程阶段注入 Display（启动时调用一次）
```

- **不可拷贝/不可移动**：成员 `RecursiveMutex` 无移动语义，拷贝/移动 `= delete` 编译期拦截
- ⚠ **不可用局部对象**：`app_main` 末尾无主循环会返回，返回时析构 `~AppStackManager` 在后台任务
  （LVGL 渲染 / BLE / HTTP 6 worker / deleteApp Task）并发运行中删栈删 App → 竞态崩溃
- ⚠ **不可用全局对象 + `std::move` 赋值**：隐式移动按值拷贝锁句柄 + 临时对象析构删除句柄 → 悬空锁 panic
- 调用方式：`AppStackManager::instance().pushToNewStack(app)` 等
- App / Display / ELF 插件全部通过 `getManager()` / `setStackManager()` **指针**间接引用，不受单例化影响，
  无需改调用点

### 架构

```
AppStackManager
├── Stack 0 [Desktop]          ← 根栈（桌面，常驻，禁止 pop 到最后）
├── Stack 1 [Game, ...]       ← 游戏独立栈（pushToNewStack 创建）
└── activeStack               ← 当前前台栈
```

### App 基类

```cpp
class App {
public:
    App(Display* display);
    virtual ~App();

    // ── 基础生命周期 ──
    virtual void init();
    virtual void deinit();

    // ── 栈生命周期钩子 ──
    virtual void onForeground();   // 成为活跃 app
    virtual void onBackground();   // 被覆盖/移出

    // ── 输入 ──
    virtual void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state);

    // ── AI 能力 ──
    virtual std::string getAiContext();                         // App 上下文 → 拼入 AI prompt
    virtual void onAiSelect(const AiSelectTarget& target);     // select 焦点
    virtual void onAiActivate();                                // activate 触发
    virtual bool executeAiAction(const AiAction& action);       // 自定义动作拦截

    // ── 便利方法 ──
    void pushApp(App* app);        // push 到当前栈
    void popApp();                 // 弹出当前栈顶
    void replaceWith(App* app);    // 替换当前栈顶

protected:
    Display* display{};
    lv_obj_t* screen{};
    bool running{};
    bool deletable{ true };
    AppStackManager* m_manager{};
};
```

### 核心规则

- **根栈 depth ≤ 1 禁止 pop** — 防止桌面被弹出
- **非根栈 pop 后为空 → 自动切回根栈 + 销毁空栈**
- **push 不删旧 app** — 旧 app 留在栈底，等 pop 才删
- **异步删除** — `scheduleDeletion()` 在独立 Task 中 `deinit()` → 等 `deletable` → **`VirtualGamepad::waitDispatchIdle()`**（防 UAF：确保 dispatchTask 当前帧 handler 已返回，不会在已删 App 上调用虚函数）→ `lockGuard` → `delete`
- **LVGL 事件回调中不可执行栈操作** — 须用 `Task::addTask` 延后（因 LVGL 锁已被持有）
- **AppStack 操作须手动去重** — AppStack 不提供内置防重入保护，`popApp()` / `replaceWith()` / `pushToNewStack()` 等操作在快速连续触发时会导致重复执行（如双击"重新开始"创建两个实例）。**所有 AppStack 操作的入口点必须手动实现防重逻辑**。

### 去重模式

```cpp
// 1. 在类中声明时间戳字段
TickType_t m_nextActionTime = 0;
static constexpr TickType_t ACTION_COOLDOWN_MS = 500;

// 2. LVGL 事件回调中加去重检查
void onRestartCb(lv_event_t* e) {
    auto* self = static_cast<MyApp*>(lv_event_get_user_data(e));

    if (xTaskGetTickCount() < self->m_nextActionTime) {
        ESP_LOGI(TAG, "多次点击，已过滤");
        return;
    }
    self->m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN_MS;

    Task::addTask([](void* p) -> TickType_t {
        static_cast<MyApp*>(p)->replaceWith(new MyApp(...));
        return Task::infinityTime;
    }, "deferredOp", self, 0, Task::Affinity::None);
}

// 3. onForeground 中设初始延迟，防止恢复前台时误触
void onForeground() override {
    m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN_MS;
}
```

### 使用方式

```cpp
class MyApp : public App {
public:
    MyApp(Display* display) : App(display) {}

    void init() override {
        App::init();
        // 创建 LVGL 对象（持锁状态下）
        if (auto guard = display->lockGuard()) {
            lv_label_create(screen);
            // ...
        }
    }

    void deinit() override {
        // 停止后台线程
        App::deinit();
    }

    void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override {
        if (state.isPressed(GamepadButton::BTN_A))
            pushApp(new NextApp(display));   // ✅ 直接调用（BLE task）
        if (state.isPressed(GamepadButton::BTN_B))
            popApp();                        // ✅ 直接调用（BLE task）
    }
};

// LVGL 事件回调中调用需延后，否则LVGL内部会出现异常（display内的元素在意料之外变化过大）
static void on_click_cb(lv_event_t* e) {
    auto* self = static_cast<MyApp*>(lv_event_get_user_data(e));
    Task::addTask([](void* p) -> TickType_t {
        static_cast<MyApp*>(p)->pushApp(new NextApp(p->display));
        return Task::infinityTime;
    }, "deferred", self, 0, Task::Affinity::None);
}
```

### 状态机

| 状态 | 说明 |
|------|------|
| `running` | 应用是否正在运行 |
| `deletable` | 应用是否可被删除（析构时需等待此标志） |

### 生命周期

1. **构造**: 简单构造，`deletable = true`
2. **init()**: 标记 `running = true`，`deletable = false`，可在此执行耗时初始化
3. **onForeground()**: app 成为栈顶，可启动定时器/传感器
4. **onBackground()**: app 被覆盖，暂停耗时任务
5. **deinit()**: 标记 `running = false`，`deletable = true`，停止后台线程
6. **析构**: 等待 `deletable == true` 后，持 LVGL 锁删除 screen 对象

---

## 应用层

所有 App 继承自 `App` 基类。系统 App 位于 `main/app/`，**游戏 App 已全部迁移为 ELF 插件**（`elfapps/<name>/`，详见 [ELF 插件化](#elf-插件化) 章节）。

### 桌面

| App | 路径 | 说明 |
|-----|------|------|
| **DesktopApp** | `main/app/desktopApp/` | 桌面启动器，**动态卡片**（扫 `/root/elf/<name>/` 插件包 + manifest.json 生成 ELF 插件卡片 + 系统卡片），3 焦点组（卡片/底部按钮/状态栏），手柄 4 方向导航 |

按钮映射：
| 操作 | 按键 |
|------|------|
| 切换卡片 | ← → |
| 开始游戏 / 打开设置 | A |
| 上一步 / 返回桌面 | B |
| 切换焦点组 | ↑ ↓ |

**动态卡片**（`onForeground → syncElfCards` 差量同步，替代原 `startElfScan` 后台渐进式扫描；锁外 `listElfApps` 扫描毫秒级，锁内三步 diff：删缺失卡/更新元数据与图标变化卡/`addElfCard` 插新卡——无变化零 LVGL 操作不闪烁，仅真实变更才动对应卡片）：
- 先 `listElfApps`（扫子目录 + 读 manifest.json，**零 dlopen**）→ 一步建完整卡片（中文名/描述/图标即时可见）
- 插件元数据（ABI v4）：`display_name`（中文名）/ `description` / `icon_path` / `name` 均已从 `elf_plugin_info_t` 移除（结构体仅剩 version/create_app/destroy_app），标识与元数据统一由插件包 `/root/elf/<name>/manifest.json` 提供（`build_elf.sh` 生成）
- 无 .so 时入口优雅提示不崩
- ⚠ `addElfCard` 防重检查须在锁内：`onForeground` 重建与历史后台扫描并发时检查在锁外会导致首启动重复卡片

### 游戏（ELF 插件）

| 插件 | 路径 | 说明 |
|------|------|------|
| **snake** | `elfapps/snake/` | 贪吃蛇。snakeGame 逻辑 + snakeRoom 房间配对 |
| **chineseChess** | `elfapps/chineseChess/` | 中国象棋。含 AI 引擎 (`chessAI.cpp`)，双人对战 |
| **fruitNinja** | `elfapps/fruitNinja/` | 水果忍者。含房间系统（FruitNinjaRoom），多玩家。渲染 canvas 化（单 lv_canvas + 脏区失效，见模块详情） |
| **tetris** | `elfapps/tetris/` | 俄罗斯方块。Host-authoritative，3 玩家（内置 P0 + 2 WebSocket 远程），共享 PieceQueue 7-bag |
| **douDiZhu** | `elfapps/douDiZhu/` | 斗地主。单机 AI + 跨主机联机（纯 TCP） |

### AI Agent

| App | 路径 | 说明 |
|-----|------|------|
| **AiAgentOverlay** | `main/app/aiAgentApp/` | AI 助手浮动窗（`lv_layer_sys`）。不占 AppStack，对话历史 + 长期记忆。可拖拽，顶部下滑/桌面卡片/手柄 **Y+X+B 组合键**唤起（实现在 gestureTimerCb；旧文档 SELECT+START 未实现）。集成 LVGL `lv_ime_pinyin` 输入法（三态：拼/五/EN，自定义词库 + 空格上屏） |

**模块路径**: `main/aiAgent/`

| 模块 | 说明 |
|------|------|
| `aiProtocol.hpp` | `AiActionType` / `AiAction` / `AiResponse`（含 `memory` + `nextRound`） |
| `aiConfig.hpp` | FAT 配置文件 + NVS API Key 加载 |
| `aiClient.hpp` | `HttpClient::send()` / `sendWithHistory()` — `esp_http_client` HTTPS POST |
| `aiParser.hpp` | cJSON 解析，鲁棒处理；提取 `actions[]`、`memory`、`nextRound` |
| `aiExecutor.hpp` | 动作顺序执行，14 个系统操作（含 Select/Activate/PowerOff 等） |
| `aiSystemInfo.hpp` | 系统信息采集：电池、WiFi、存储、内存、手柄电量等 |

**AiAction 字段**（所有系统操作共用）：

| 字段 | 类型 | 用途 |
|------|------|------|
| `action` | string | 操作类型（必填） |
| `text` | string | 字符串参数（app名、焦点组名、回复内容等） |
| `value` | number | 数值参数（亮度、音量、焦点索引等） |
| `params` | object | 扩展参数（复杂 JSON，如 wifi_connect 的 ssid/password） |

**AiResponse 顶层字段**：

| 字段 | 类型 | 用途 |
|------|------|------|
| `actions` | array | 操作列表 |
| `memory` | string (可选) | 长期记忆，增量更新 |
| `nextRound` | bool (可选) | true=本轮执行后继续下一轮（如 App 切换后探索新界面） |

**App 基类 AI 接口** (`main/app/app.hpp`)：

```cpp
struct AiSelectTarget {
    std::string group;   // 焦点组名（空=单维度焦点）
    int index{};         // 组内索引
};

class App {
    // 上下文描述 → 拼入 {app_context}
    virtual std::string getAiContext();

    // 焦点系统（系统级，由 AiExecutor 路由）
    virtual void onAiSelect(const AiSelectTarget& target);   // select 动作
    virtual void onAiActivate();                              // activate 动作

    // 自定义动作拦截（可选覆盖）
    virtual bool executeAiAction(const AiAction& action);
};
```

所有带焦点导航的 App 均已接入 `onAiSelect()` + `onAiActivate()`：
- DesktopApp、BleSettingsApp、WifiSettingsApp、TimeSettingsApp
- PowerManagementApp、TetrisRoomApp
- FruitNinjaRoom、SnakeRoom、ChineseChessRoom

**Agent 多轮协作流程**：

```
用户输入
  ↓
① HttpClient::send(systemPrompt, userInput, history)
  ↓
② AiParser::parse(response) → AiExecutor::execute(actions)
  ↓
③ 如果 response.nextRound == true 且未达 3 轮上限：
   → 从模板重建 system prompt（刷新 {app_context} 为新 App 信息）
   → 再次调用 AI 继续完成目标
   ↓ (最多 5 轮 follow-up)
④ 记录 memory（长期记忆） + 对话历史（最近 8 轮，含 follow-up 合成轮；`MAX_HISTORY_TURNS=8`）
  ↓
⑤ 等待用户下一轮输入
```

**14 个系统操作**：

| 操作 | 说明 | 示例 |
|------|------|------|
| `respond_text` | 回复文字 | `{"action":"respond_text","text":"你好"}` |
| `open_app` | 打开应用 | `{"action":"open_app","text":"tetris"}` |
| `select` | 移动焦点到元素 | `{"action":"select","text":"cards","value":2}` |
| `activate` | 触发当前焦点 | `{"action":"activate"}` |
| `navigate_back` | 返回上一级 | `{"action":"navigate_back"}` |
| `navigate_home` | 回桌面 | `{"action":"navigate_home"}` |
| `set_brightness` | 调亮度 0-100 | `{"action":"set_brightness","value":50}` |
| `set_volume` | 调音量 0-100 | `{"action":"set_volume","value":70}` |
| `play_audio` | 播放音频 | `{"action":"play_audio","text":"/path/file.mp3"}` |
| `wifi_connect` | 连接 WiFi | `{"action":"wifi_connect","params":{...}}` |
| `system_info` | 获取设备信息 | `{"action":"system_info"}` |
| `power_off` | 关机 (Deep-sleep) | `{"action":"power_off"}` |
| `generate_lua_game` | AI 生成 Lua 游戏并运行 | `{"action":"generate_lua_game","text":"贪吃蛇","params":{"code":"..."}}` |
| `read` | 读取文件内容回传 AI（配 `nextRound:true`） | `{"action":"read","text":"/root/lua/game/test.lua","nextRound":true}` |

**支持**：对话历史（最近 8 轮）+ 长期记忆（AI 通过 `memory` 字段增量更新）+ 多轮协作（`nextRound` 控制）+ App 焦点探索（`select`/`activate`）

**手柄按钮映射**（键盘显示时）：

| 操作 | 按键 |
|------|------|
| 键盘按键导航 | ← → ↑ ↓（左摇杆） |
| 确认按键/选候选字 | A / L3 |
| 退格删除 | B |
| 发送消息 | Y |
| 键盘/候选面板间切换焦点 | X |
| 中英文模式切换 | 键盘底部 `🔣` 按钮 |
| 候选面板翻页 | ↑ ↓（焦点在候选面板时） |

### 设置

| App | 路径 | 功能 |
|-----|------|------|
| **BleSettingsApp** | `main/app/bleSettingsApp/` | BLE 手柄扫描/连接/断开/删除配对，NVS 持久化 |
| **WifiSettingsApp** | `main/app/wifiSettingsApp/` | WiFi AP 扫描/连接/断开，密码输入键盘 |
| **TimeSettingsApp** | `main/app/timeSettingsApp/` | 手动调时 / SNTP 自动同步 / 时区设置 |
| **PowerManagementApp** | `main/app/powerManagementApp/` | 主机+手柄电量显示 / 关机 (Deep-sleep) / 低功耗 |

### 其他

| App | 路径 | 说明 |
|-----|------|------|
| **LuaManagerApp** | `main/app/luaManagerApp/` | Lua 脚本管理器：浏览/运行/删除/重命名脚本，AI 生成入口；市场游戏按游戏 ID 显示、禁用重命名、删除=递归删目录 |
| **MarketApp** | `main/app/marketApp/` | 游戏市场：Lua+ELF 混合截图卡片网格（类型徽标）+ 详情 + 下载/更新/卸载（Lua 存 `/root/lua/market/`，ELF 装 `/root/elf/`） |
| **TestApp** | `main/app/testApp/` | 测试用，开发调试 |

### Tetris 模块详情

**路径**: `elfapps/tetris/main/app/tetris/`

| 目录 | 内容 |
|------|------|
| `gameLogic/` | 游戏核心逻辑：`PlayerState`（单个玩家状态）、`TetrisClient`（输入/状态管理）、`GameState`（序列化快照） |
| `renderer/` | LVGL canvas 渲染器 (`tetris_renderer.cpp`)，棋盘=1 个 `lv_canvas` + 预览=4 个 `lv_canvas`（PSRAM RGB565 缓冲 + 局部失效），将 GameState 绘制到屏幕 |
| `net/` | WebSocket 网络层 (`tetris_net.cpp`)，Host/Client 模式，JSON 消息协议 |
| `net/tetrisTcpHost.hpp/.cpp` | **TCP 游戏服务器**（端口 8083），异步 send Task 20fps，主机→从机 snapshot |
| `net/tetrisRemoteClient.hpp/.cpp` | **Tetris 远程客户端**，listen 端口 8084 等待主机连接，recv Task 解析 snapshot |
| `tetrisApp.cpp` | 主游戏 App |
| `tetrisRoomApp.cpp` | 房间选择 App（含远程模式跳转） |
| `tetrisRemoteApp.cpp` | **远程游戏 App**，全屏渲染远程主机画面 |

**渲染性能设计**（TetrisRenderer canvas 化）：
- 棋盘/预览**不使用逐格 LVGL 对象**：原 220（棋盘）+ 64（预览）个 cell 对象在锁块消行时批量样式更新，会使 LVGL 失效缓冲溢出（早期 `LV_INV_BUF_SIZE=32`）→ **整屏软件重绘** → 卡顿。现 `LV_INV_BUF_SIZE=256` 由 CMake 编译宏注入（见 [Patches](#patches) 段末「LVGL 构建选项覆盖」）。
- 现改为：棋盘 = 1 个 `lv_canvas`（PSRAM RGB565 缓冲），预览 = 4 个 `lv_canvas`；方块/ghost 直接画进棋盘缓冲（`paintBoardCell`，ghost 用 `blend565` 30%/50% 混合）；`syncState` 用 `lv_obj_invalidate_area` 只失效变化区域（锁块时 5~8 条失效 < 32），日常移动仍是局部绘制 + 局部失效。
- **⚠ 坑**：`lv_obj_invalidate_area(obj, area)` 的 `area` 是**屏幕坐标**（非对象本地坐标），需先用 `lv_obj_get_coords` 换算（见 `invalidateBoardLocal`）；`lv_obj_invalidate(obj)` 整对象失效自动用正确 coords。
- 完整实现见 `elfapps/tetris/main/app/tetris/renderer/tetris_renderer.hpp/.cpp`。

**消息协议** (JSON 文本帧, `ws://{host}:8080/ws/tetris` 或 TCP `host:8083` / `client:8084`):
| type | 方向 | 说明 |
|------|------|------|
| join | C→S | 加入游戏 |
| join_ack | S→C | 分配 player_id, player_count |
| input | C→S | 按键事件 |
| snapshot | S→C | 完整 GameState |

**跨主机联机流程**：
```
主机 TetrisApp::init()
  ├── tetrisNetInit(true, ...)      — 启动 WS 服务器 :8080/ws/tetris（Web 客户端）
  ├── tetrisTcpHostStart(8083)       — TCP 服务器 + 异步 send Task（20fps）
  ├── tetrisTcpHostSetPlayerCount()  — 通知客户端游戏人数
  └── tetrisTcpHostConnectToClient(ip, 8084)
       └── TCP 连接从机 → 分配 s.clients[] slot + recv Task（接收 join/input）

从机 TetrisRemoteApp::init()
  ├── m_client.listen(8084)          — 监听等待主机连接
  └── accept → sendJoin() → 收到 snapshot → TetrisRenderer::syncState()
```

### FruitNinja 模块详情

**路径**: `elfapps/fruitNinja/main/app/fruitNinja/`

| 文件 | 内容 |
|------|------|
| `fruitNinjaLogic.hpp/.cpp` | 游戏核心逻辑（纯 C++，无 LVGL）：水果物理/切片/粒子/方向+按钮匹配 |
| `fruitNinjaRenderer.hpp/.cpp` | **canvas 化渲染器**：单 `lv_canvas` 软件绘制 + 脏区集合失效（游戏层零 LVGL 对象） |
| `fruitNinjaApp.cpp` | 主游戏 App（游戏循环 60fps + 多指触摸轮询 GT911Indev 槽位） |
| `fruitNinjaRoom.cpp` | 房间系统（多玩家配对） |

**渲染性能设计**（FruitNinjaRenderer canvas 化）：
- **根因**：原对象池模式每帧 ~300+ LVGL 对象操作（40 水果 lv_image + 40 粒子 + ≤128 刀光段×2 层 lv_line），
  累计失效区域超过 `LV_INV_BUF_SIZE=256`（`lv_refr.c:332` 溢出即写整屏）→ 整屏光栅化 + 水果 shadow 模糊 → 游戏线程 CPU ~20%。
- **现架构**：游戏层（水果+粒子+分隔线+刀光+提示文字）画进**单个 `lv_canvas`**（1280×720 RGB565 PSRAM ~1.76MB），
  **游戏层零 LVGL 对象**：
  - 直接操作 canvas buffer 指针（**勿用 `lv_canvas_set_px`**——逐像素 invalidate 陷阱，见 canvas.h 文档）
  - 软件绘制：`fillRect`（首行填充 + 后续行 memcpy 复制，内存带宽级）、`fillCircle`（扫描线：行内求半宽直接填区间 + 顶部高光按行 blend）、
    `blitImage`（水果图片圆形裁剪 + 最近邻缩放 blit）、`drawWideLine`（点到线段距离圆头线）、`blend565`
  - **脏区集合失效**：`collectDirty`（变化对象 上帧bbox∪本帧bbox，精确不合并；水果 bbox 含顶部提示区域 56×36px）
    → `expandRedraw`（**与 D 相交一次遍历，无 O(n²) 传播**——paint 按 z 序从底到顶绘制，被覆盖对象天然正确）
    → `paint`（清 D → 按 z 序重画 R：水果→粒子→分隔线→刀光→提示文字）
    → 对 D 逐区域 `lv_obj_invalidate_area`（屏幕坐标换算，参考 tetris `invalidateBoardLocal`）
  - **LVGL 内置 `lv_refr_join_area`(lv_refr.c:618) 免费做自适应合并**：相邻且合并面积有利的区域自动合并成簇，分散区域保持精确小面积（无需应用层 AABB 合并，避免虚胖）
  - 首帧强制整 canvas 失效（`m_initialInvalidated`）
- **帧前/提交分离（收缩锁范围）**：`beginFrame()`（无锁：collectDirty + expandRedraw，纯计算）在 `gameLoop` 无锁区执行；
  `render()`（持锁：paint 写 buffer + invalidate + HUD）→ 持锁窗口最小化，减少与 LVGL 刷新的锁竞争
- **提示文字（↑A/↓B）**：↑↓←→ + A/B/X/Y 共 8 字符 glyph 预缓存——`create()` 持锁用 `lv_font_get_glyph_dsc`/`lv_font_get_glyph_bitmap`
  取 A8 位图，按 `header.stride` 逐行复制成紧凑 PSRAM 私有副本；每帧**无锁**软件 blit（定位公式与 LVGL 一致 `lv_draw_label.c:624`）。
  ⚠ freetype IMAGE 模式 `lv_font_get_glyph_bitmap` 返回 `lv_draw_buf_t*`（**非像素数组**）、位图 stride 对齐——不能直接 memcpy（会乱码）
- **刀光**：段池（kMaxSegments=64）+ **超时消失**（`TRAIL_LIFETIME=0.5s`，`ageTrail` 递减 life），纯色不透明单层粗线（无逐帧淡出/半透明，性能最优）；仅在新增/移除/回绕顶掉时标脏
- **水果**：图片化——`esp_driver_jpeg` 硬件解码 5 张 JPG（`JPEG_DECODE_OUT_FORMAT_RGB565` + 小端序，无需 RGB888→565 转换）
  → PSRAM（>128px 边软件最近邻缩到 128，省内存 + 加速 blit）→ `blitImage` 软件圆形裁剪 + 最近邻缩放 blit；
  任一图失败自动回退纯色圆 + 顶部高光。⚠ buffer 必须用 `jpeg_alloc_decoder_mem` 分配（驱动 DMA 对齐要求，
  `heap_caps_malloc` 普通分配会报 `not aligned`）；`jpeg_decoder_process` 的 outbuf_size 传 `allocated_size`
- **移除全部 shadow**（模糊重算是性能杀手）
- HUD / 退出按钮 / GameOver 遮罩 保留 LVGL 对象（低频/轻量）
- **性能实测**：游戏线程 CPU 47%→~7%（5a：提示标签进 canvas + 去 expandRedraw O(n²) + fill 优化 + 收缩锁范围），
  普通 FPS ~31（目标 30 达标）、CPU ~35%；极端情况（大量粒子+刀光）FPS ~10，剩余瓶颈 = `paint` 写大脏区（fruitNinja 49%），
  可选项：粒子/刀光数量上限、Phase 5b 双缓冲并行（把 paint 也挪出锁）
- 完整实现见 `elfapps/fruitNinja/main/app/fruitNinja/fruitNinjaRenderer.hpp/.cpp`

---

## ELF 插件化

**系统架构核心机制**：游戏编译为 **ELF 插件 (.so)** 放 FAT 分区 `/root/elf/`，运行时 `dlopen` 动态加载。系统固件（桌面 + 基础库 + 系统 App）常驻，主固件 ~4.79MB。改一个游戏只重编译该游戏（`tools/build_elf.sh`），HTTP PUT 上传即生效，**无需重烧主固件**。

```
主固件（C++ monolith）                        .so 插件工程（elfapps/<name>/，C++）
├─ elfSdk 层                                  ├─ 唯一 C 入口 elf_plugin_entry()
│   ├─ elf_symtab.c    — 符号表（脚本生成）   │   └─ 返回 elf_plugin_info_t
│   ├─ elf_sdk.cpp     — 注册符号表 + 启动    │       { version, create_app, destroy_app }
│   │                   优化（日志静默 +      │         （标识与元数据统一走
│   │                   符号哈希 resolver）    │         插件包 manifest.json）
│   └─ gen_elf_syms.py — 白名单生成工具       ├─ XxxElfApp : public App（.so 内 new）
├─ ElfManager                                  ├─ 自建事件循环/线程（原生灵活性）
│   ├─ 内部 RAM 栈任务 dlopen + version 校验   └─ 调用主固件符号（lockGuard/Task/单例…）
│   ├─ refcount + size/mtime 热更新检测
│   └─ AppStack delete 后 → dlclose
└─ AppStack（对 ELF App 与原生 App 一视同仁）
```

### elfSdk — ABI 契约 + 符号表

**模块路径**: `main/elfSdk/`

- `elf_sdk.h`：`elf_plugin_info_t`（ABI v4：`version`/`create_app`/`destroy_app`；标识与展示元数据统一走插件包 manifest.json）+ `ELF_SDK_VERSION` 守卫（插件/主固件同编译头文件，版本不匹配拒绝加载）
- `elf_symtab.c`：**单一符号表** `g_main_elfsyms`（2133 符号，`tools/gen_elf_syms.py` 从主固件 `.elf` 白名单生成，`elf_sdk_init()` 在 main.cpp 早期注册）
- **启动优化**（`elf_sdk_init()` 内）：
  - **日志静默**：`esp_log_level_set("ELF"/"DLMOD", WARN)` 过滤 esp_elf_loader 组件逐符号 INFO（启动 ~850 行→0），错误级保留
  - **符号解析哈希化**：FNV-1a 2048 桶哈希索引（PSRAM，name/sym 指针零拷贝直指符号表内）+ `elf_set_symbol_resolver(elfSdkResolve)`——每次 dlopen 重定位的符号查找从线性 strcmp（~1000+ 次）→ O(1)，未命中兜底 `elf_find_sym_default`；零组件 patch
- **白名单过滤**（脚本顶部）：C 函数前缀（`lv_`/`esp_log_`/`cJSON_`/`lwip_`/`httpd_`/`esp_websocket_`/`xQueue`）+ C++ 类方法（`^Class::` 反解匹配）+ `std::`/`__cxa_`/`operator new` 运行时 + `DATA_EXACT`（`__dso_handle` 等数据符号）+ `CPP_FN_PREFIXES`（无类名的 C++ 文件级函数）
- ⚠ static inline 的 LVGL API 不在 `.elf` → 不导出，.so include `lvgl.h` 自行内联
- **符号保活**：主固件没引用的符号（被 GC）→ `main/CMakeLists.txt` `target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=<sym>")`（**必须 INTERFACE**，main 是静态库，PRIVATE 静默失效）

### ElfManager — 插件模块管理

**模块路径**: `main/elfManager/`

- 缓存：`std::map<std::string, Module>`（path→{ handle, info, refcount, size, mtime }）
- `ensureLoaded`：持久**内部 RAM 栈**任务 dlopen + dlsym + 版本校验；检测 size/mtime 变化 → refcount==0 时重载（热更新）
- `createApp(path, display, params)`：refcount++ → `info->create_app` → `App::setElfModule(path)`
- `onAppDestroyed`：AppStack 在 `delete app` **之后**调用 → refcount-- → 0 时 `dlclose`（不能在 deinit 时 dlclose——析构还在跑 .so 代码会崩）
- **插件包格式**：`/root/elf/<name>/{manifest.json, <name>.so, icon.jpg}`（`build_elf.sh` 产出 `build/pkg/`）。元数据全部外置 manifest（name/display_name/description/main/icon），桌面扫描**零 dlopen**：`listElfApps()`（扫子目录）→ `readManifest()`（FAT + cJSON）一步建卡，秒级全显示

### 生命周期

```
push(ElfManager::createApp("/root/elf/snake/snake.so", ...))
  └─ refcount++ → so->create_app() → new XxxElfApp（继承 App，虚分派进 .so）
AppStack: init/onForeground/onGamepadInput/onBackground … 全部走 .so vtable
pop → scheduleDeletion 任务: deinit() → delete app（虚析构在 .so 内跑）
  └─ delete 后 → onAppDestroyed → refcount-- → 0 时 dlclose
```

### 桌面动态扫描 + AI 动态路由

- **桌面**：`onForeground → syncElfCards`（锁外 `listElfApps` 扫 `/root/elf` 子目录 + 读 manifest.json，**零 dlopen** → 锁内 diff：删除缺失卡 / 更新元数据+图标变化卡（图标换 `lv_obj_get_child(card,0)` 的 image src）/ `addElfCard` 插新卡，无变化零渲染不闪烁）；图标 `F:elf/<name>/icon.jpg` 由 LVGL 直接解码
- **AI**：`AiAppTarget.elfPath` 字段，`open_app` 优先走 `ElfManager::createApp`；`getOpenableApps()`/`getAiContext()` 动态并入 ELF 插件（name=manifest.name）

### 插件工程（`elfapps/<name>/`）

- 模板：唯一 C 入口 `elf_plugin_entry()` + `XxxElfApp : public App` + `dsoHandle.c`（`void* __dso_handle`，`.so` `-nostdlib` 链接对 shared 目标做硬检查）+ sdkconfig.defaults（**`CONFIG_FREERTOS_HZ=1000` 必须与主固件同步**，否则 `pdMS_TO_TICKS` 时序差 10 倍；WS 插件另加 `CONFIG_HTTPD_WS_SUPPORT=y`）+ `plugin_meta.json`（name/display_name/description 元数据单一来源）+ `icon.jpg`（桌面卡片图标，开发者自备）
- 编译：`-fPIC -fno-exceptions -fno-rtti -fvisibility=hidden`，`--whole-archive` 链接 `libmain.a` 成 `.so`；与主固件**同头文件 + 同 sdkconfig**（防类布局漂移）
- 构建：`tools/build_elf.sh <name>`（**必须 Ninja** 生成器；自动跑 `check_elf_syms.sh` 检查 UND 符号对照 `.elf`，含弱符号，缺失即拦截；构建后按 sdkconfig 优化级别自动 `--strip-debug`——DEBUG/NONE 保留调试信息，SIZE/PERF 剥离，`.dynsym` 不受影响 dlsym 安全；最后产出**插件包** `build/pkg/`：`<name>.so` + `manifest.json`（由 plugin_meta.json 生成，main/icon 按目录名补全）+ `icon.jpg`，并打印 curl PUT 上传命令）
- 音频：插件用 `Audio` → `EXTRA_COMPONENT_DIRS` 复用主工程 managed_components 的 6 个音频组件（仅头文件，.so 不链接音频代码）

### 插件编写规则（ABI 坑）

- **禁止文件级非 POD static**：loader **不执行 `.init_array`** → 文件级对象不被构造（=UB），默认成员初始化器也不生效（BSS 零初始化 → 运行时 bug）。规范改法：
  ```cpp
  struct S { int nextPlayerId = 1; std::function<void()> cb; };
  static S& getS() { static S inst; return inst; }   // 函数局部 static，首次调用真构造
  ```
- **`__dso_handle` 插件自足**：`dsoHandle.c` 提供（勿在主固件自定义——ESP-IDF cxx 组件已定义会重定义）
- 单例/框架方法跨边界走**单符号表**；std::string 跨边界返回同工具链可行
- 32 位目标 ABI：`size_t`=unsigned int → `_Znwj`（非 `_Znwm`）；C++14 sized delete `_ZdlPvj`（.so 会引用，需导出）；成员函数参数 `App*` → 替换符 `S_`（`_ZN3App7pushAppEPS_`）；const 成员 `K` 紧跟 `_ZN`（`_ZNK...`）
- ⚠ 不要手推 mangled 名，一律 `nm` / `gen_elf_syms.py --search` 核实
- loader 内置 libc 表**没有** `snprintf`/`memcmp` → 需自导出；`ESP_LOGI` 宏引用 `esp_log`/`esp_log_timestamp`（App 内联虚函数里也有，插件继承编译带出）→ 需导出
- 已核实正确（无需改）：`_ZN3AppC1/C2EP7Display`、`_ZN3AppD1/D2Ev`、`_ZN14VirtualGamepad8instanceEv`、`_ZN10BleGamepad8instanceEv`、`_ZNK10BleGamepad14connectedCountEv`、`_ZN14BatteryManager{10getPercent,12getVoltageMv,10isCharging}Ev`
- ⚠ **check_elf_syms.sh 拦不住白名单漏类**：脚本对照主固件 `.elf`（有定义→绿），但运行时解析用符号表 `g_main_elfsyms`（白名单过滤）——两个不同集合。白名单漏类只能运行时炸 `Can't find common`（tetris 漏 `HostManager`/`BleSettingsApp` 实测）。**新增/迁移插件后应跑 "UND vs 符号表" 预检**（`nm -u <so>` 对照 `g_main_elfsyms` 而非 .elf）
- ⚠ **.so 体积根因**：IDF 全局注入 `-ggdb -gdwarf-4`（`tools/cmake/build.cmake` 硬编码），**无任何 sdkconfig 选项能关**；.so 文件本身就是产物 → 调试信息占 50-70%（douDiZhu 877KB→strip 后 97KB）。改优化级别只动代码段，打不掉大头。**调试方法**：release 插件无源码级调试（崩溃只剩裸地址、addr2line 不可用）→ 把插件 sdkconfig 优化级别改为 DEBUG 重编（自动保留调试信息），或重建时临时改回

### P4 关键坑

1. **PMP 使 PSRAM heap 不可执行**：`CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=y` → ELF 代码取指 `Instruction access fault`。**必须关闭**
2. **加载任务栈须在内部 RAM**：`esp_elf_arch_flush` 调 `spi_flash_disable_interrupts_caches_and_other_cpu`（assert 栈在内部 RAM）→ ElfManager 用 `xTaskCreateStatic` + 内部 RAM 静态栈；⚠ **dlclose 无 arch_flush**，普通 PSRAM 栈可跑
3. **组件管理**：独立 .so 工程必须用 `EXTRA_COMPONENT_DIRS` 复用主工程 managed_components

### 构建流程（新增/更新游戏）

```
1. git mv 游戏源码 → elfapps/<name>/main/（目录对齐主固件 include 路径）
2. 插件工程：CMakeLists + plugin.cpp + dsoHandle.c + sdkconfig.defaults + plugin_meta.json + icon.jpg
3. 主固件：删游戏源 + desktopApp 卡片/AI 路由改 ElfManager::createApp
4. 重生成符号表：gen_elf_syms.py（移除独占 inline 符号被 GC 的"鸡生蛋"坑）
5. build_elf.sh <name> → 上传 build/pkg/ 下 3 个文件（建目录 + .so + manifest + icon）→ 桌面自动出现
```

### 回归验证清单（插件改动后）

- **R1 基础链路**：dlopen/dlsym/版本校验/失败路径 ✅ std::vector/string + new/delete→PSRAM ✅ 文件级 vs 函数局部 static（fileStatic=0x0，loader 不执行 .init_array）✅ 虚分派生命周期 ✅ 退出→refcount=0→dlclose 干净 ✅
- **R2 运维**：热更新（改→编→PUT→重进见新，主固件不重烧）✅ 单例直出 ✅ PSRAM 无泄漏（多轮开关无异常增长，`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` 测量）✅ 无 .so 兜底不崩 ✅
- **Phase 2/3 游戏**：游戏表现与原生一致（FREERTOS_HZ=1000 同步后）✅ AI open_app 动态路由 ✅ 网络符号（tetris WS/TCP 8083、douDiZhu TCP 8086 双板联机）✅

---

## 网络层

### WiFi — STA/AP 双模

**模块路径**: `main/wifi/wifi.hpp`

C 风格函数 API，非类封装。

| 函数 | 说明 |
|------|------|
| `wifiInit(bool remote)` | 初始化 WiFi 栈，`remote=true` 通过 C6 实现 |
| `wifiStart()` / `wifiStop()` | 启动/停止 WiFi 驱动 |
| `wifiStationStart/Stop()` | STA 模式控制 |
| `wifiApStart/Stop()` | AP 模式控制（默认 SSID: `esp32p4`） |
| `wifiConnect(ssid, pass)` | 连接 WiFi |
| `wifiStationScan()` | 同步扫描 AP 列表 |
| `wifiNatStart/Stop()` | NAT 转发控制（STA→AP 客户端） |

**启动策略**:
1. 尝试 STA 模式连接保存的 WiFi
2. 连接失败 → 启动 AP 模式（SSID: `esp32p4`）
3. NAT 自动将 STA 网络共享给 AP 客户端

### C6 Slave — 协处理器管理器

**模块路径**: `main/wifi/slave.hpp`

```cpp
class Slave {
    static Slave& instance();
    void start();        // 后台 Task 中早期启动 SDIO 复位 (~1.5s)
    void waitReady();    // 阻塞等待协处理器就绪
    bool isReady() const;
};
```

- `start()` 在 `app_main()` 早期（NVS 初始化后）通过 `Task::addTask` 后台执行 SDIO 复位
- 与显示/LVGL 初始化并行，节省 ~1.5s 启动时间
- 内部使用 `EventGroup` (`kReadyBit`)，支持多 Task 并发等待
- `BleGamepad::initEspHostedBt()` 中的第二次调用因 transport 已 up 而立即返回

### SocketStream — TCP 流封装

**模块路径**: `main/wifi/socketStream.hpp`

| 类 | 说明 |
|------|------|
| `ISocketStream` | TCP 输入流：`get()`, `peek()`, `read()`, `getline()` |
| `OSocketStream` | TCP 输出流：`put()`, `print()`, `sendNow()` |
| `IOSocketStream` | 同时继承输入+输出流 |

内部维护 64 字节输入缓冲。

### SocketStreamWindow — 连接池

**模块路径**: `main/wifi/socketStreamWindow.hpp`

```cpp
class SocketStreamWindow {
    bool setSocket(Socket socket);    // 占用一个窗口
    IOSocketStream& getSocketStream();
    bool closeSocket(bool blocked);   // 释放窗口
    static size_t getEnabledCount();  // 当前活跃连接数
};
```

HTTP 服务器使用 6 个窗口作为工作者线程池。

### HTTP Server

**模块路径**: `main/server/`

| API | 说明 |
|------|------|
| `serverStart(maxRetry=3)` | 启动 HTTP 服务器（裸 socket, 端口 80, 6 workers） |
| `serverStop()` | 停止服务器 |
| `serverIsStarted()` | 检查运行状态 |

- 基于 `SocketStreamWindow` 连接池，6 个工作者线程从池中取连接处理
- 内置 HTML 文件服务（`buildinHtml/`）
- 静态资源目录：`/root/`（FAT 分区）

#### 内置文件管理页（`/file`）

**模块路径**: `main/server/buildinHtml/file.html` + `file.hpp`（`constexpr char HtmlFile[]` 编译进固件，`serverKernal.cpp:503` 在 uri=`/file|/File|/FILE` 时返回）

| 功能 | 实现 |
|------|------|
| 上传进度条 | **纯前端** `xhr.upload.onprogress`（`e.loaded/e.total`），与 OTA 页面同款；设备端零参与 |
| 上传方式 | `xhr.send(file)` 直发 File 对象（浏览器自动设 Content-Length + 流式读取，不再 FileReader 整读内存）；后端 `httpPut` 本就是流式写入（512B 块循环）→ 天然配合 |
| 上传中按钮禁用 | 防并发 PUT 写坏文件，`onload/onerror` 恢复 |

- ⚠ **`file.html` ↔ `file.hpp` 需同步转义，无自动脚本**：改 HTML 后必须手动转义（`\`→`\\`、`"`→`\"`、换行→`\n`、tab→`\t`，单引号不转义）重新生成 hpp（可用 python `repr()` 或 `replace` 链），漏同步则页面不更新
- ⚠ **上传 URL 必须拼文件名**：`uploadPressed` 中路径 = `place + file.name`（place 是当前目录，以 `/` 结尾）。漏拼文件名 → 文件内容被 PUT 到**目录 URL** → 服务端走建目录分支（不落盘、body 残留污染下一请求）。曾实测：manifest.json 上传后 PUT `/elf/fruitNinja/` body 196B → 下一请求解析出 `path="name":` 诡异 POST
- ⚠ **响应缺失 Content-Type = Firefox 报 "XML 解析错误"**：XHR 规范规定响应无 Content-Type 时默认按 `text/xml` 解析（body 非合法 XML → 控制台报错；功能正常因代码读 `responseText`，解析失败只影响 `response` 文档对象）。曾踩坑：`sendOk`（PUT/DELETE 的 OK 响应，body="OK"）与 `/api/getSpace` 均缺头 → 已补 `text/plain; charset=utf-8`。**新增响应分支务必设置 Content-Type**

### OtaManager — 双槽 OTA 空中升级（封壳设备唯一升级途径）

**模块路径**: `main/ota/otaManager.hpp` / `.cpp`

单例类（Meyers' Singleton，`OtaManager::instance()`），复用现有裸 socket HTTP 服务器实现 OTA 接收端：

| API | 说明 |
|-----|------|
| `POST /api/ota` | **流式写入 OTA 分区**（浏览器/curl 上传 `build/The-Defect.bin`，成功后 1s 自动重启） |
| `GET /api/otaInfo` | JSON 状态：running/next 槽位 + 版本、last_invalid（失败槽）、last_rollback（NVS 记录）、各槽状态 |
| `checkRollback()` | 启动早期（延迟 10s）比对 pending 槽 → 判定上次升级是否回滚（NVS 记录）。**不自动确认固件**——回滚确认全手动（见下） |
| `POST /api/ota/confirm` | **手动确认当前固件**：OTA 升级后的固件处于 `PENDING_VERIFY`（回滚窗口打开），用户验证正常后调用 → `esp_ota_mark_app_valid_cancel_rollback()` 标记 VALID 关闭窗口（无需重启） |
| `POST /api/ota/setBoot` | 手动切换下次启动分区（body `{"slot":"ota_1","invalidate":false}`；`invalidate=true` 同时标记当前分区无效=手动回退；成功 1s 后重启） |

**单例化**：
- `instance()` 用 C++11 magic static（函数局部 static），**语言保证**首次构造仅一次 + 线程安全（并发调用者阻塞等待）
- 构造函数内创建内部 RAM 常驻任务（`xTaskCreateStatic` + 静态栈 8192 words）+ `xQueue` 请求队列；
  原 C 风格的 `otaInternalStackInit()` / 懒初始化互斥量 `s_otaInitMutex` 全部删除（竞争从"手动纪律"变为"语言保证"）
- `internalStackWorker` 经 `xTaskCreateStatic` 参数传 `this` 访问成员队列；实例为函数局部 static → `.bss` 内部 RAM
- 全部 API 改为成员方法：`begin/write/abort/finish/precheckImage/switchBootPartition/confirmRunning/getInfoJson`
  等；镜像头常量 `OtaHeaderSize` → `OtaManager::HeaderSize`（512）
- `main.cpp`：`serverStart()` 前单线程阶段显式 `OtaManager::instance()` 触发构造；延迟 10s 任务注册点
  也移到启动流程末尾（`serverStart` 前），执行时刻 ≈ serverStart + 10s（比原 display.init 后稳定）

**流程**（`httpOta`，serverKernal.cpp）：
```
POST /api/ota（body 不缓冲，socket 流式读取）
  → 并发标志（6 worker 互斥）→ 目标分区检查 → 大小预检
  → 读镜像头（512B）→ precheckImage（magic + 版本防重烧）
  → esp_ota_begin(OTA_WITH_SEQUENTIAL_WRITES) → 记录 pending 槽到 NVS
  → 循环 readByte(4KB) → esp_ota_write
  → esp_ota_end（镜像校验）→ esp_ota_set_boot_partition → 1s 后 esp_restart
```

**回滚可观测性（封壳设备无串口，靠自身暴露状态）**：
- 升级前 `begin()` 把目标槽写入 NVS `ota.pending_slot`；重启后 `checkRollback()` 比对 running：
  - `pending != running` → 目标槽没跑起来 → 回滚发生 → 记录 NVS `ota.last_rollback` + `ota.last_rb_sec`（boot 秒）
  - `pending == running` → 升级成功，仅清除 pending
- `esp_ota_get_last_invalid_partition()` 检测失败槽（`ABORTED`/`INVALID`），`esp_ota_get_state_partition` 读槽状态（valid/pending_verify/aborted/empty…）
- **版本防重烧**：上传镜像版本与 last_invalid 相同 → 400 拒绝（防反复烧入坏固件）

**回滚确认（全手动）**：`checkRollback()` **不自动** `mark_app_valid_cancel_rollback`。
- 首次 USB 烧录固件 = `UNDEFINED`（出厂信任，无需确认）；OTA 升级固件 = `PENDING_VERIFY`（回滚窗口打开）
- 未确认时**任何重启**都会触发 bootloader 自动回退旧版（安全网保持最大效力）
- 用户验证正常后：web 页面「确认此固件」→ `POST /api/ota/confirm` → VALID 关闭窗口
- 手动切换 `setBoot` 的目标槽同样进入待确认状态，需再次确认

**手动分区切换 / 回退（运行中崩溃兜底）**：
- 10s 自动回滚只保护"启动早期崩溃"；固件 VALID 后运行中崩溃重启仍回同一固件
- `POST /api/ota/setBoot`（body `{"slot":"ota_1","invalidate":false}`）：用户决定下次启动分区
  - `invalidate=true` = 手动回退：当前分区标 INVALID（`esp_ota_mark_app_invalid_rollback`，失败仅警告）
  - `esp_ota_set_boot_partition` 把目标槽重置为可启动（含之前 ABORTED/INVALID 的槽，官方"启动无效/中止应用"标准做法）
  - 校验：目标槽存在 / 非当前运行 / 镜像有效（`esp_ota_get_partition_description`）

**关键配置**：
- 分区表：`ota_0` + `ota_1` 双 6M 槽（fat 偏移 16M 不变，数据零迁移）
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`：未确认固件崩溃/重启 → bootloader 自动回退旧版，防封壳变砖
- `main/CMakeLists.txt` PRIV_REQUIRES 含 `app_update esp_app_format esp_partition esp_timer bootloader_support`
- 升级页面：FAT 静态资源 `resourse/server/ota/index.html`（`/server/ota/` 前缀映射），显示当前/目标版本 + 待确认警告条 + 回滚警告条 + 分区管理（切换/回退）+ 上传进度

**⚠ P4 内部 RAM 三原则**（OTA 全链路实测踩坑，违反必崩）：
1. **调用 OTA API 的任务栈必须在内部 RAM**：`esp_ota_*` 内部会 mmap flash /
   `spi_flash_disable_interrupts_caches_and_other_cpu`（禁用 cache 期间栈在 PSRAM = assert 崩溃）。
   server coWorker 栈 / `Task::addTask` daemon 栈均为 PSRAM → 必须经 `runInInternalStack` 执行。
2. **`esp_ota_write` 的源缓冲必须在内部 RAM**：写 flash 禁 cache 窗口内读 PSRAM = 非法访问
   （PSRAM 依赖 cache）→ 栈溢出 + 堆损坏连锁崩溃。用 `getWriteBuffer()`/`getHeaderBuffer()`
   提供的静态 `.bss` 缓冲（`MALLOC_CAP_SPIRAM` 或栈数组都踩过坑）。
3. **内部任务用常驻 + 队列**（`runInInternalStack` 实现）：单例构造时创建常驻 `otaInt` 任务
   （`xTaskCreateStatic` + 内部 RAM 静态栈 8192 words）+ `xQueue` 请求队列。
   空闲时阻塞零 CPU；**不要**"临时创建 + `vTaskSuspend` + `vTaskDelete`"——cache 暂停期
   交叉核访问已释放栈会踩内存（`Bad tail` / `multi_heap` 损坏）。静态栈/缓冲放 `.bss`
   （普通 static，勿加 `EXT_RAM_BSS_ATTR`）；写缓冲已移出栈后实测 8192 words 余量充足。

**⚠ HTTP body 非阻塞读取残留**（`readBodyFully`，serverKernal.cpp）：
- `ISocketStream::read()/readByte()/getline()` 均非阻塞（IBuffer 仅 64B，`MSG_DONTWAIT`），
  单次调用可能读不满 body → 残留字节被同一 TCP 连接的下一个请求误当请求行（诡异 POST，
  如 `path=ota_1","invalidate":false}`）。**所有 body 读取必须循环读完**
  （`readBodyFully()`：循环 `readByte` + `isGood` 断连检查 + `vTaskDelay(1)`）。
- 已覆盖：`/api/floor`、`/api/wifiStart`、`/api/apStart`、`/api/formatFlash`、
  `/api/aiConfig`、`/api/mdnsConfig`、`httpDelete`、`httpOtaSetBoot`、`httpOta`。
- ⚠ **`httpPut` 建目录分支（path 以 `/` 结尾）不读 body 也会残留**：floor 创建无需 body
  （`newFloor` 发空 body），但客户端若带 body PUT 到目录 URL（如误把 manifest 上传到
  `/elf/<name>/`），196B 残留被解析成下一个请求（实测 `POST "name":`）。已修复：建目录分支
  循环 discard body（`readByte` 512B 块 + `isGood` 断连检查），空 body 零开销。
- `/api/formatFlash` 原 VLA `char body[bodyLenght+1]`（栈上，恶意大 body 栈溢出）→ 已改堆分配。

**⚠ 路由长度必须精确**（`stringCompare` 严格长度匹配）：
- `stringCompare(uri, len, "/api/xxx", n)` 的 `n` 是**字符串精确长度**，手数极易错；
  长度不符恒 false → handler 静默不执行（`/api/ota/setBoot` 曾传 15 实际 16，切换"没反应"）。
- 新增路由建议用 `sizeof("/api/xxx") - 1`，或注释标注长度。


### WS Server — WebSocket 服务器

**模块路径**: `main/wsServer/`

| API | 说明 |
|------|------|
| `wsServerStart()` | 启动 WebSocket 服务器（端口 8080） |
| `wsServerStop()` | 停止 |
| `wsServerRegisterWs(uri, handler)` | 注册 WebSocket handler |
| `wsServerUnregister(uri)` | 注销 |
| `wsServerSendText(fd, data, len)` | 发送文本帧到指定客户端 |
| `wsServerBroadcastText(data, len)` | 广播文本帧到所有客户端 |

**配置**: `max_uri_handlers = 8`, `max_open_sockets = 8`, 单 worker task (RealTime 优先级)

**注册机制**: 各游戏模块自行注册 handler（如 Tetris: `/ws/tetris`），不修改 wsServer 代码

### mDNS

**模块路径**: `main/wifi/mdns.hpp`

| API | 说明 |
|------|------|
| `mdnsInit()` / `mdnsDeinit()` | 初始化/反初始化 |
| `mdnsStart(hostname, instance)` | 设置主机名和实例名（默认 `"esp32p4"`） |
| `mdnsServiceAdd(name, "_http", "_tcp", 80)` | 注册 HTTP 服务 |
| `mdnsServiceAdd(name, "_ws", "_tcp", 8080)` | 注册 WS 服务 |
| `mdnsServiceAdd(name, "_p4console", "_tcp", 80)` | 注册专属服务类型，供设备发现用 |
| `mdnsGetConfiguredHostname()` | 从 NVS 读取持久化的主机名，未配置返回 `"esp32p4"` |
| `mdnsSetHostname(hostname)` | 保存到 NVS 并热重启 mDNS（deinit→init→重注册所有服务） |

**主机名自定义**：
- NVS 持久化（namespace `"mdns"`，key `"hostname"`）
- 修改后立即生效，无需重启
- 服务实例名自动跟随派生：`{hostname} HTTP` / `{hostname} WS`
- 主机名校验：仅字母、数字、连字符，最长 48 字符
- 配置入口：WiFiSettingsApp 底部"主机名"行 / HTTP API `GET/POST /api/mdnsConfig`

### Discovery — 设备发现

**模块路径**: `main/discovery/`

```cpp
struct DiscoveredConsole { char hostname[64]; char ip[16]; uint16_t port; };

int discoverConsoles(DiscoveredConsole* consoles, int maxCount, uint32_t timeoutMs = 3000);
```

- 查询 `_p4console._tcp` 服务，返回局域网内其他游戏主机的 hostname、IP、端口
- 自动过滤自身（对比当前活跃 hostname）
- 同步阻塞请求

### ConsoleManagerApp — 多主机管理

**模块路径**: `main/app/consoleManager/`

- 通过 mDNS 发现其他主机，列表展示（hostname + IP:port）
- 桌面状态栏图标（索引 6）进入
- 支持手动扫描
- **BLE 配对风格界面**：4 槽位 + P0-P3 掩码按钮 + 交换/断开
- **访问/从机双视图**：连接后自动切换
- **每槽掩码控制**：4 个小按钮决定该 (hostId, playerId) 对是否进入本地 App
- **交换模式**：点击 [交换] → ←→ 选择目标槽 → A 确认 → 主机交换 hostId + mask 并广播
- **活动指示**：手柄输入时槽位底色变亮（同 BleSettingsApp）
- 槽位交换同时交换 **mask**，从机通过 hostname 匹配自动更新 `m_mySlot`
- 聚焦导航：CARD → MASK → ACTIONS 三阶层级
- AI 入口：`open_app("consoleManager")`

---

## 游戏市场

从网络市场服务器浏览并下载/更新社区游戏，**支持两类内容**：
- **Lua 脚本**（`type="lua"`）→ 存 `/root/lua/market/<作者>/<游戏>/`，运行统一走「脚本管理」LuaManagerApp
- **ELF 原生插件**（`type="elf"`）→ 装 `/root/elf/<id>/` 插件包（manifest.json + .so + icon.jpg），返回桌面后 `onForeground` 全量重建自动出现游戏卡片，直接运行

市场**只负责下载/更新/卸载，不自动运行**。服务端与游戏内容为独立仓库 `The-Defect-Market/`（git submodule）。

**模块路径**: `main/market/`（客户端）+ `main/app/marketApp/`（UI）

### 设备端协议

| 接口 | 说明 |
|------|------|
| `GET /api/scripts?type=lua\|elf` | 列表（JSON，含 name/author/version/desc/tags/rating/downloads/thumb/screenshot/download + type/main/manifest/icon） |
| `GET /api/search?q=&type=` | 按名称/作者/标签/简介搜索（返回与列表同构完整条目） |
| `GET /games/<作者>/<游戏>/<file>` | 脚本 / .so / 缩略图 / 大图 / manifest（静态文件） |
| `POST /api/stats/download` | 下载量 +1（下载成功自动上报；`id` 传组合键 `作者/id`，同名游戏不串号） |

- 服务器地址：`/root/system/market_config.json` 的 `server_url` 字段
- `MarketClient`（`main/market/marketClient.hpp`，header-only）异步封装 GET/POST，`.so` 二进制与文本走同一 `downloadFile`
- `MarketConfig`（`main/market/marketConfig.hpp`）从 FAT JSON 读服务器地址；`marketConfigSaveUrl(url)` **设备端写回**（header-only：读现有配置保留 `timeout_ms` → cJSON 构造 → `testFloor` 确认目录存在、仅缺失时 `newFloor`（⚠ `newFloor` 直接 `mkdir`，目录已存在返回 `EEXIST=false` 会误报"无法创建"）→ `OFile` 覆写；⚠ `OFile::open` 无 `O_TRUNC`，先 `removeFile` 再写防旧字节残留）
- **类型判定**：服务器扫描时目录含 `manifest.json` → `type="elf"`（否则 "lua"）；ELF 条目的 `main`（.so 文件名）/ `manifest`（URL）/ `icon`（URL）由服务端补全
- **无版本校验**：`version` 字段仅详情页显示（info.json 提供）；「更新」永远 = 重新下载覆盖，不做版本比较

### MarketApp — 市场 UI

| 特性 | 说明 |
|------|------|
| 入口 | 桌面「游戏市场」卡片；AI `open_app("market")` |
| 一级界面 | **3 列截图卡片网格，Lua/ELF 混合显示**；每卡左上角**类型徽标**（Lua=蓝 0x0066cc / ELF=紫 0x8a2be2）；右上角「已装」角标（`m_cardBadges` 动态刷新，下载/卸载即时更新）；缩略图懒加载串行队列，未加载显示 ★评分 占位 |
| 详情 | 大图（CONTAIN 自适应，支持横屏/拼接长图）+ 类型/作者/版本/更新时间/下载量/评分 + 简介 |
| 下载/更新 | 按 `s.type` 分流：Lua 存 `/root/lua/market/<作者>/<游戏>/`；ELF 走 `downloadElf()`（`newFloor("/root/elf/<id>")` → 依次下载 manifest.json → .so → icon.jpg(可选)）。`testFile()` 检测已装 → 按钮「更新」，未装 → 「下载」 |
| 卸载 | 详情页已装时显示「卸载」按钮（焦点索引 2）：统一入口 `uninstall()` 分流——ELF=`ElfManager::unload` + `removeFloorRecursive(/root/elf/<id>)`；Lua=`removeFloorRecursive(/root/lua/market/<作者>/<游戏>)` + 清空作者目录 |
| 输入 | 网格 4 向导航；A 直接进详情；最后一行 ↓ 进底部栏（刷新/详情/下载或更新/服务器）；B 返回 |
| **服务器编辑** | 底部栏第 4 按钮 `LV_SYMBOL_SETTINGS "服务器"`（`BOTTOM_BTN_COUNT=4`）→ `showServerDialog()` 编辑弹窗（复用 WifiSettingsApp 弹窗模式：`lv_layer_top` 遮罩 + textarea 预填当前地址 + 软键盘 + 取消/确定）。确认 → `confirmServerDialog()`：去首尾空白 → 空输入拒绝 → 无 `://` 自动补 `http://`（裸 IP:端口友好）→ 去末尾斜杠 → `marketConfigSaveUrl()` 写 FAT → 更新 `m_serverUrl` + 自动 `fetchList()`（等效连接测试）。弹窗期间手柄走键盘路由（A/L3/START 按下选中键、摇杆 `lv_group_send_data`、B 取消） |

- **运行入口**：Lua 在「脚本管理」（LuaManagerApp）中运行；ELF 返回桌面自动建卡（`onForeground → syncElfCards` 差量同步）直接运行；市场更新 icon 覆盖（路径不变）后 `lv_image_cache_drop(NULL)` 失效缓存防残留旧图（`lv_image_cache_drop` 为 LVGL 9 private API，需 `#include "src/misc/cache/lv_cache.h"`）
- **已装识别**：Lua=`testFile(本地脚本路径)`；ELF=`testFile(/root/elf/<id>/manifest.json)`（不读 .so，避免 dlopen）
- **卸载后清理**：Lua 删除后清空空作者目录；ELF 删除后 `ElfManager::unload`（refcount=0）再递归删目录，返回桌面卡片消失
- **拉取竞态防护**：`fetchList()` 捕获发起时 `m_serverUrl` 快照，回调比对——请求期间地址被改（编辑弹窗确认）→ 丢弃旧响应并立即用新地址重拉，防旧服务器数据覆盖新状态
- **弹窗生命周期**：`deinit()` 关闭服务器编辑弹窗（`lv_layer_top` 上创建，App screen 删除不会自动清理）

---

## Screen Stream Architecture

**模块路径**: `main/screenStream/`

### 原理

`ScreenStream` 单例类。直接读取 bridge 层的帧缓冲，零额外渲染、零额外拷贝。

```
LVGL 渲染 → bridge flush → 帧完成
  → on_frame_ready(disp, disp_fb, size, ctx)    ← 每帧触发一次
                                ↓
ScreenStream::frameReadyCallback:
  → 缓存 fb 指针

调用方（HTTP handler / 定时器）:
  → ScreenStream::captureJpeg(buf, size)
  → jpeg_enc_process(DMA 读 PSRAM → 编码)      ← 硬件 JPEG，0 拷贝
```

### 关键设计

| 特性 | 说明 |
|------|------|
| **零额外渲染** | 读取 bridge 已有的 `disp_fb`（front buffer），无需 snapshot |
| **零额外拷贝** | JPEG 编码器 DMA 直读帧缓冲，不经过 CPU |
| **零开销空闲态** | 串流关闭时，bridge 中仅 1 次 null 指针检查/帧 |
| **PSRAM + DMA** | DMA2D/PPA 写入和 JPEG 编码器读取都走 DMA，绕过 CPU 缓存 |
| **旋转兼容** | `disp_fb` 已由 bridge 旋转到物理方向（720×1280） |
| **线程安全** | 回调只写指针，`captureJpeg()` 用普通读取，对齐指针在 RISC-V 上是原子的 |

### Bridge 补丁

ScreenStream 依赖 `esp_lvgl_adapter` 的自定义修改，以 patch 形式分发。详见 [Patches 章节](#patches)。

### 调用方式

```cpp
ScreenStream::instance().start(&display, 720, 1280);
ScreenStream::instance().captureJpeg(buf, size);
ScreenStream::instance().stop();
```

### wsStreamHandler — /ws/stream 双任务管线（自愈模型）

**模块路径**: `main/screenStream/wsStreamHandler.cpp`

编码任务（`wsEnc`）+ 发送任务（`wsSend`）通过双缓冲 + 计数信号量（2）+ Queue(2) 传递帧，真正并行。

| 特性 | 说明 |
|------|------|
| 帧格式 | 二进制 WS 帧（0x82），手写帧头 + `send()` 载荷 |
| **完整发送** | `sendAllBytes()` 循环推进：**部分发送=正常进度**（绝不丢弃尾部——丢弃即截断帧，浏览器解析器永久错位）；`EAGAIN/EWOULDBLOCK/EINTR` 5ms 退避重试，**帧时限 2s**（`kFrameDeadlineMs`） |
| **SO_SNDTIMEO 收紧** | 串流 fd 发送超时 5s→1s（`setsockopt`，启动/切换时设置），快速失败快速重连 |
| **失败自愈** | 帧发送失败 → `httpd_sess_trigger_close` 主动关连接 + 停管线 → 浏览器 `onclose` → 5s 自动重连全新流。**禁止**静默累计错误后任务自杀（旧版 50 次错误后 `vTaskDelete` 且句柄不清理 → 重启门 `!s_encodeTask && !s_sendTask` 永假 → 流永久死亡，连接却保持打开） |
| **任务退出清理** | 两个任务自删前必须 `s_encodeTask/s_sendTask = nullptr`；send task 退出前 drain 队列归还缓冲 |
| **重启重建** | `startStreamTasks()` 每次重建 sem/queue（防计数漂移）；重建前等待旧 encode task 完全退出（删对象前 take = UB） |
| **客户端切换** | 新客户端 GET 一律挂 `PendingClient`（`portMUX` 保护 hd/fd 一致性），send task 帧边界原子切换（唯一消费者），不粗暴杀旧管线 |
| **广播隔离** | `wsStreamIsFd(fd)` 供 `wsServerBroadcastText` 跳过串流 fd，杜绝 httpd 任务与 send task 并发写同一 socket 帧交错 |

**浏览器端自愈**（`resourse/server/screen/screen.js`）：`createImageBitmap` 连续解码失败 ≥5 次（`MAX_DECODE_FAILS`）→ `wsStream.close()` 强制重连（成功路径清零计数）。

### 截图（纯前端，零后端接口）

串流页「📷 截图」按钮：**直接复用 ws 已收到的 JPEG 帧**（`wsStream.onmessage` 拷贝保存为 `latestFrameBytes`）→ `createImageBitmap` → 复用 `ROTATE_CANVAS` 旋转为横屏 1280×720 → `toBlob` → `window.open(objectURL)` 新标签打开（用户右键保存）。

| 特性 | 说明 |
|------|------|
| **零后端改动** | 不走 HTTP/WS 新增接口，**不触发服务端 JPEG 编码**——避免 coServer 在 PSRAM 栈执行 `captureJpeg` 的 cache/中断禁用崩溃（曾实测崩溃：`coServer00` 栈 PSRAM，日志输出路径进入临界区异常） |
| 依赖串流 | 必须已连接 `/ws/stream` 且有帧到达（断连后保留最后一帧仍可截图）；未连接时按钮提示先开始串流 |
| 延迟 | 保存的是最近一帧（网络延迟 ≤ 数百 ms），手动截图无感 |
| 方向 | 与串流显示一致：竖屏帧 → 前端 Canvas 旋转横屏 |

---

## 媒体与外设

### Audio — 音频子系统

**模块路径**: `main/audio/`

#### ES8311 编解码器

```cpp
class ES8311 {
    struct Config {
        GPIO i2s_mck, i2s_bck, i2s_ws, i2s_dout;  // I²S 引脚
        GPIO pa_pin;                                 // 功放使能
        bool pa_reverted;
        bool use_mclk = true;
        uint8_t codec_addr = 0x30;
    };
    bool init(IIC& iic, Config config);
    bool open(esp_codec_dev_sample_info_t* fs);
    bool play(const void* data, int len);
    void setVolume(int percent);
};
```

引脚配置：
| 信号 | GPIO |
|------|------|
| I²S MCLK | 13 |
| I²S BCK | 12 |
| I²S WS | 10 |
| I²S DOUT | 9 |
| PA 使能 | 53 |

#### Audio Manager — 音频管理器

**`Audio`** — Meyer's Singleton，管理 `esp_audio_render` 多流混音渲染器。

**`AudioHandle`** — 值类型，效仿 `std::thread` 生命周期模型。⚠ **禁止拷贝**（拷贝构造/赋值 `= delete`）：拷贝共享同一 Impl，任一拷贝析构都会 stop 播放——只允许移动（返回值走 move，impl 移交不误 stop），与 `std::thread` 一致。

```cpp
// 绑定模式（handle 析构时停止播放）
AudioHandle bgm = Audio::play("/root/music/bgm.mp3");
bgm.setLoop(true).setVolume(0.8).play();

// detach 模式（解绑生命周期）
Audio::play("/root/sfx/click.wav").setVolume(0.5).detach();
```

**移动语义**：`AudioHandle` 支持 move 构造/赋值（`std::swap(impl)`），返回值走 move 而非拷贝，规避旧版「局部 handle 析构误 stop」问题。持有句柄时可用 `handle = {}`（右值 → 移动赋值）把旧 `impl` 移交临时对象，临时析构按绑定模式触发 `stop()` —— **即用此模式停止循环播放**（如 BatteryMonitor 关闭弹窗 `alertAudio = {}` 停止循环提示音）。

**依赖组件**: `esp_codec_dev`, `esp_audio_codec`, `esp_audio_render`, `gmf_audio`, `esp_audio_effects`

**数据流**:
```
File (FAT) → encBuf → [esp_audio_dec_process] → pcmBuf → [*volume] → stream_write
                                                                         ↓
                                                               Mixer Thread
                                                                         ↓
                                                               outWriter → ES8311
```

**主音量**: `Audio::setMasterVolume(int)` 委托到 ES8311 硬件，NVS 持久化。

#### ES8311 电源管理

通过 render 的 `OPENED`/`CLOSED` 事件回调自动管理 ES8311 开关：
- 首个流打开 → `ES8311::open()`（DAC 上电）
- 末个流关闭 → `ES8311::close()`（DAC 断电）
- 使用 `Mutex` 保护 `open/close` 并发

```cpp
// main.cpp 不再需要手动 audio.open(&config)
audio.init(iic, config);                 // 仅初始化 I2S + codec 接口
audio.setVolume(100);
Audio::instance().init(audio);           // render 通过 event_cb 自动开关
```

#### AudioSample — 预解码缓存

将音频文件预解码为 PCM 存入 PSRAM，播放时**零 SD I/O、零解码**。

```cpp
class AudioSample {
    bool load(const char* path);         // AAC/MP3 → PCM (PSRAM)
    AudioHandle play(bool loop = false); // 从缓存播放
    int  activeCount() const;            // 当前播放实例数（应用层限流）
    void _releaseInstance();             // 内部：播放结束回调
};
```

**重采样到 48000Hz**：`load()` 解码后自动用 `esp_ae_rate_cvt` 重采样到 48000Hz，播放时 render 的 per-stream rate_cvt 检测到 `src==dest` 进入 **bypass 零缓冲**模式，数据直通 ringbuffer。

```cpp
// 一次加载，多次播放（独立流，共享 PCM）
static AudioSample s_sfx;
if (!s_sfx.isValid()) s_sfx.load("/root/sd/sfx/click.aac");

// 限流示例：限制 3 个实例
if (s_sfx.activeCount() < 3)
    s_sfx.play().detach();
```

#### 自适应限速

PCM 缓存模式下 `memcpy` 极快（~0.01ms），远超 mixer 消费速度。按实时播放时长做自适应限速：

```cpp
uint32_t paceMs = batch * 1000 / pcmSampleRate;   // 这批应播时长
uint32_t elapsed = (esp_timer_get_time() - tick) / 1000;
if (paceMs > elapsed + 1)
    vTaskDelay(pdMS_TO_TICKS(paceMs - elapsed - 1));
```

**注意**：render 的 ringbuffer `acquire_write` 有已知 bug（源码 TODO 注释），实际不阻塞。多流模式下必须靠上层限速。

#### 流管理与限流

| API | 返回值 | 用途 |
|-----|--------|------|
| `Audio::usedStreamCount()` | 总活跃流数（含 BGM） | 系统级限流 |
| `sample.activeCount()` | 特定 AudioSample 实例数 | 采样级限流 |
| `MaxStreamNum = 14` | 硬上限 | `uint16_t streamUsedMask` |

`activeCount` 在 `play()` 时原子递增，task 退出时自动递减，线程安全。

#### 已知坑

| 问题 | 说明 |
|------|------|
| **render acquire_write 不阻塞** | `esp_audio_render.c` 中 `write_rb` 的 TODO 注释承认逻辑有误，多流模式下 ringbuffer 满时不阻塞，数据静默丢弃。PCM 模式需上层自适应限速 |
| **NRVO 析构误 stop（已修复）** | 旧版仅拷贝语义时 `Audio::play(AudioSample&)` 返回局部 handle，NRVO 未发生时拷贝构造返回值 → 局部 handle 析构误 `stop()`；`AudioHandle` 加入移动语义后返回值走 move（impl 移交），不再误 stop |
| **rate_cvt flush** | `esp_ae_rate_cvt_process(nullptr, 0)` 触发 `Invalid parameter` 错误，虽然不影响数据但日志难看。应省略 flush 步骤 |

### BLE Gamepad — 无线手柄

**模块路径**: `main/bleGamepad/`

#### 架构

```
ESP32-P4 (NimBLE host)
    ↕ esp_hosted (SDIO)
ESP32-C6 (NimBLE controller + 蓝牙射频)
    ↕ BLE
ESP32-C6 手柄 (x4 max)
```

#### 关键类

```cpp
class BleGamepad {
    static BleGamepad& instance();
    bool start(Display* display);
    void startScan();
    void stopScan();
    void connect(uint8_t scanIndex);
    void disconnect(uint8_t playerId);
    void disconnectAll();
    uint8_t connectedCount() const;
    uint8_t getBatteryLevel(uint8_t playerId) const;
    void syncPairedToNvs();
    std::vector<PairedDevice> getPairedDevices() const;
};
```

#### GamepadState

```cpp
struct GamepadState {
    uint16_t buttons;        // GamepadButton 位图
    uint8_t lx{128}, ly{128}; // 左摇杆 (0~255)
    uint8_t rx{128}, ry{128}; // 右摇杆
    uint8_t lt{0}, rt{0};    // 扳机
    uint8_t dpad{15};        // D-pad (0-7=方向, 15=松开)
    bool isPressed(GamepadButton btn) const;
};
```

#### GamepadButton 位定义

| 按钮 | 位 | 说明 |
|------|-----|------|
| BTN_A | bit 0 | A 键 (SOUTH) — 确认 |
| BTN_L3 | bit 13 | 左摇杆按下 |
| BTN_START | bit 11 | 开始 |
| BTN_SELECT | bit 10 | 选择 |

#### 手柄固件

BLE 手柄使用独立的 ESP32-C6 固件项目，通过 Git Submodule 挂载在 `gamepad/`：

> **[esp32c6HidJoystick](https://codeberg.org/loouwiit/esp32c6HidJoystick)** — ESP32-C6 BLE HID Gamepad 固件

游戏市场服务器与社区游戏内容同样以 Git Submodule 挂载在根目录 `The-Defect-Market/`：

> **[The-Defect-Market](https://codeberg.org/loouwiit/The-Defect-Market)** — 游戏市场服务器 + 社区游戏仓库（**Lua 脚本 + ELF 原生插件**；静态内容走文件系统，动态数据走 SQLite；目录含 manifest.json 自动判定 type=elf）

```bash
git clone --recurse-submodules https://<本仓库地址>
# 或已克隆后补拉子模块：
git submodule update --init --recursive
```

手柄固件与主机通过 NimBLE GATT 协议通信，定义 `GamepadButton` 位图（见 `gamepadState.hpp`）和输入事件队列。双方需保持协议同步。

> 📖 手柄固件完整文档见 [`gamepad/readme.md`](../gamepad/readme.md)。

#### 特性

- **上限 4 玩家** (`MaxPlayers = 4`)
- **自动重连**: 启动后扫描 5 秒，自动连接 NVS 中保存的已配对设备
- **NVS 持久化**: `syncPairedToNvs()` 将 BDA+name 写入 NVS
- **输入队列**: FreeRTOS Queue 做线程安全的输入事件传递
- **Battery Level**: 通过 BLE Battery Service 读取手柄电量

#### NimBLE 注意事项

| 问题 | 说明 |
|------|------|
| `EXT_ADV` 禁用 | 使用 legacy API 时必须关闭 `CONFIG_BT_NIMBLE_EXT_ADV` |
| GAP 回调不调 `ble_gap_connect` | 须用 `Task::addTask` 延迟 1tick 执行 |
| esp-hosted 异步 `EBUSY` | `disc_cancel()` 异步 → `connect()` 返回 `EBUSY`，200ms 重试 |
| `m_scanning` 标志同步 | 手动扫描时必须同步设置标志，否则 `stopScan()` 无效 |

### Battery Manager — 电池管理

**模块路径**: `main/battery/`

```cpp
class BatteryManager {
    static BatteryManager& instance();
    bool init();
    int getPercent();         // 0~100, -1=失败
    int getVoltageMv();
    bool isCharging();
    static void startChargingAnim(lv_obj_t* label);
    static void stopChargingAnim(lv_obj_t* label);
    static const char* getIcon(int percent);    // LV_SYMBOL_BATTERY_*
    static lv_color_t getColor(int percent);   // ≥61%绿, 21~60%黄, ≤20%红
};
```

**硬件配置**: ADC1_CH6, 分压 5.1kΩ+10.2kΩ, 充电检测 GPIO23 (低电平=充电中)

**充电动画**: 300ms 定时器循环取模（0→1→2→3→4→0→...，`(step+1)%5`，非 ping-pong）

### BatteryMonitor — 低电量检测提醒（主机 + 蓝牙手柄）

**模块路径**: `main/battery/batteryMonitor.hpp` / `.cpp`

系统级单例：周期 Task（15s）轮询主机 `BatteryManager` 与 4 个手柄槽位 `BleGamepad`，单阈值穿越判定后弹出 `lv_layer_sys()` 系统级弹窗（列出所有低电量设备）+ 循环提示音。

| 项 | 说明 |
|----|------|
| 阈值 | 主机 `kHostLowPercent=20`、手柄 `kPadLowPercent=20`（constexpr 可调）；连续 2 次低于阈值确认防抖 |
| 弹窗 | `lv_layer_sys()` 全屏半透明遮罩（拦截底层触摸）+ 居中卡片（⚠「电量不足」+ 每行设备 + [知道了]） |
| 关闭 | [知道了] 按钮 或 任意手柄新按键（`VirtualGamepad::getState(0,p)` 边沿检测，100ms LVGL 定时器） |
| 仅提醒一次（按设备） | 每源 `alerted`+`dismissed`；用户关闭仅影响当前设备，其他设备后续掉电仍触发提醒；充电/回升/断连 → 该源复位 |
| 输入屏蔽 | 弹窗显示时 `isAlertShown()` → VirtualGamepad 停止向底层 App 分发（同 AI Overlay 门控） |
| 提示音 | `AudioHandle alertAudio` 持有循环播放 `/root/system/music/alert.aac`；关闭弹窗 `alertAudio = {}`（移动赋值）停止 |
| 接线 | `main.cpp`：`AiAgentOverlay::start()` 之后 `BatteryMonitor::instance().start(&display)` |

**状态机（每源）**：`low`（当前低于阈值，主机还需非充电）+ `alerted`（已提醒）+ `dismissed`（用户已关闭）+ `confirmTicks`。`tick()` 每周期读电量 → 更新状态机 → 汇总「低电 && 已触发 && 未关闭」设备 → 更新弹窗；`newlyAlerted`（新设备触发）时播放提示音。

### SystemMonitor — 系统监视器（CPU + 内存 + FPS）

**模块路径**: `main/monitor/`

```cpp
class SystemMonitor {
    static SystemMonitor& instance();
    void start(Display* display);
    void stop();
    void setBrief(bool);     // 简略模式（仅输出总结行）
};
```

- **整合原 CpuMonitor（CPU 差分采样）与 fpsMonitor（独立 Task）**，并新增内存统计，单 Task 每秒输出
- **CPU**：通过 FreeRTOS `uxTaskGetSystemState()` 差分采样（简略模式走 IDLE 计数器轻量路径）
- **内存**：内部 RAM 与 PSRAM 的**已用/总量(已用%)**（`heap_caps_get_free/total_size` 差值，显示 used 与现代系统一致）
- **FPS**：`Display::getFps()`（display 未初始化时自动跳过，`lv_disp` 判空保护）
- **输出示例**（brief 模式，每秒两行）：
  ```
  [SysMon] CPU: 23%, C0: 21%, C1: 25%
  [SysMon] RAM:248KB/415KB(59%) PSRAM:7.3MB/28.1MB(26%) FPS:60
  ```
- **内存无泄漏验证**：ELF 插件 dlopen/运行/退出（refcount=0→dlclose）前后 PSRAM 已用无增长
- 双缓冲持久分配（`m_prevSnapshot` / `m_currSnapshot`），避免每轮 malloc/free

---

## 日志与崩溃诊断（封壳设备远程调试）

**模块路径**: `main/logService/`（LogService 单例）+ `resourse/server/diag/`（Web 页面）

封壳设备无串口，日志与崩溃现场通过 Wi-Fi 远程获取。入口：浏览器 `http://<主机名>.local/server/diag/`。

### printf 约定（封壳设备远程调试）

主固件**所有实际编译的 printf 日志均已改造为 ESP_LOGx**（`serverKernal.cpp` TAG=server、
`fat.cpp` TAG=fat、`mem.cpp` TAG=mem、`socketStream.cpp` TAG=socketStream）——
printf 直出 UART 无法被 `esp_log_set_vprintf` 重定向捕获，封壳后不可见。

- **新增日志一律用 ESP_LOGx（带 TAG）**，禁止 `printf` 输出日志
- 残留的 `printf` 仅存在于**不编译的调试代码**内（`#if false` / `#if MemDebug && MemProcessDebug` 等），
  打开调试宏前需同步改为 ESP_LOG（如 `main/storage/mem.cpp` 的 VFS 回调调试打印）
- 级别参考：错误 `ESP_LOGE` / 警告 `ESP_LOGW` / 生命周期 `ESP_LOGI` / 高频请求级 `ESP_LOGD`

### LogService — 日志重定向服务

```cpp
class LogService {
    static LogService& instance();
    void start();          // mountFlash + nvsInit 后调用：注册 vprintf + 启动 LogTask（幂等）
    bool readTail(char* buf, size_t bufSize, size_t maxLines);  // 尾部 N 行
    size_t getFileSize() const;
    void setGlobalLevel(int level);   // 0-5（NONE..VERBOSE），NVS 持久化
    int  getGlobalLevel() const;      // NVS，未设置返回 -1
    void setFileLevel(int level);     // 落盘阈值（0-5，默认 3=INFO）：仅级别号 ≤ 阈值写入文件
    int  getFileLevel() const;        // 当前生效落盘阈值
    void setWsLevel(int level);       // WS 推送阈值（0-5，默认 3=INFO）：仅级别号 ≤ 阈值推送
    int  getWsLevel() const;          // 当前生效 WS 阈值
    void setTagLevel(const char* tag, int level);   // 按 tag 源头过滤（esp_log_level_set）+ NVS 持久化
    void clearTagLevel(const char* tag);            // 移除 tag 过滤（恢复全局）
    int  getTagLevel(const char* tag) const;        // 未设置返回 -1
    size_t getTagLevelCount() const;                // 已设置 tag 数（≤8）
    bool getTagLevelAt(size_t i, char* tagOut, size_t tagOutSize, int* levelOut) const;
    void detectCrash();               // 启动早期：复位原因 + coredump 存在 → NVS 记录
    uint32_t getPanicCount() const;
    time_t getLastCrashTime() const;
    void clearCrashRecords();
    bool hasCoredump() const;
    void addWsClient(httpd_handle_t hd, int fd);   // /ws/log 订阅
    void removeWsClient(int fd);
    void enqueue(const char* line, size_t len, int level);  // vprintf 回调入队（带级别）
};
esp_err_t logWsHandler(httpd_req_t* req);          // /ws/log handler（main.cpp 注册）
```

**架构**（vprintf 回调可重入，多任务并发调用）：

```
ESP_LOGx
  ├─ esp_log 源头筛选（按 tag：esp_log_level_set）→ 不达标日志根本不产生
  ↓
logVprintf（esp_log_set_vprintf 回调）
  ├─ vsnprintf → 解析级别（fmt 首字符）→ 入队 LogItem{line, level}（64×260B，满则丢弃）
  ├─ 链式调用旧 handler（保留 UART 输出）
  └─ FAT 挂载前：写 PSRAM 早期缓冲（16KB），start() 后 LogTask 落盘
  ↓
LogTask（独立低优先级任务，内部 RAM 静态栈）
  ├─ dump 早期缓冲 → app.log
  ├─ 出队 → 出口分级：
  │    level ≤ file_level → fwrite 追加 app.log（攒批 4KB / 200ms flush + fsync）
  │    level ≤ ws_level   → 推送 /ws/log 订阅客户端（断连自动移除）
  ├─ 文件 >512KB → 滚动（app.log → app.log.prev）
  └─ 每 10s status 自检：lines/bytes/err/drop/file/queue/fileLvl/wsLvl
```

**两级过滤**：
1. **源头（esp_log 层）**：`setTagLevel(tag, level)` → `esp_log_level_set`（vprintf **之前**生效，
   不达标的该 tag 日志根本不产生；NVS 持久化 `tag_N`/`taglv_N` 固定 8 槽，start() 恢复）
2. **出口（LogTask）**：`file_level`（落盘）/ `ws_level`（WS）独立阈值分流

**级别语义**：级别号越小越严重（NONE=0, ERROR=1, WARN=2, INFO=3, DEBUG=4, VERBOSE=5），
“保留”条件 = `level ≤ 阈值`。例：`file_level=1` + `ws_level=3` → **仅 ERROR 落盘、WS 显示 INFO+**；
`tag="SysMon", level=2` → SysMon 只产生 WARN+（源头就掐掉 INFO，所有出口生效）。

**关键设计**：
- **vprintf 回调零阻塞**：只做 vsnprintf + 入队（xQueueSend 0 超时）；文件 IO / WS 推送全部在 LogTask
- **va_list 消费一次**：`va_copy` 给 vsnprintf，原 `va_list` 传给旧 handler（否则二次使用 UB）
- **链式 UART**：`esp_log_set_vprintf` 返回旧 handler，开发阶段串口日志不丢失
- **队列满丢弃新行**：日志风暴时保系统稳定（丢日志 < 拖垮调用方）
- **崩溃记录**（NVS namespace `"log"`）：`detectCrash()` 在 nvsInit 后调用，`esp_reset_reason()` 判定 panic/看门狗/欠压 → `panic_count`++ + `last_crash` 时间戳；coredump 存在性读分区首字（0xFFFFFFFF=空）
- **日志级别（三级独立）**：全局 `esp_log_level_set`（决定“产生”什么）+ `file_level`（落盘阈值）+ `ws_level`（WS 阈值），均 NVS 持久化，`start()` 恢复；`POST /api/logLevel` 远程调整（DEBUG 排查用）。级别解析：ESP_LOGx 宏展开后 fmt 首字符即级别字母（E/W/I/D/V）
- **/ws/log 订阅**：`wsServerRegisterWs` 注册，`max_uri_handlers=8` 已用 5 个（touch/stream/input/console/log），上限内

### HTTP 诊断 API（serverKernal.cpp）

| 接口 | 说明 |
|------|------|
| `GET /api/log?tail=N` | 日志文件尾部 N 行（默认 200，≤2000），text/plain + `no-store` |
| `POST /api/logLevel` | 全局/分级：`{"level":4,"file_level":1,"ws_level":3}`（缺省不改动）；按 tag：`{"tag":"BleGamepad","level":1}`（缺省 level=移除该 tag），NVS 持久化 |
| `GET /api/resetInfo` | JSON：`reset_reason`/`reset_reason_name`/`panic_count`/`last_crash`/`has_coredump`/`log_level`/`file_level`/`ws_level`/`tag_levels`（对象映射） |
| `POST /api/resetInfo/clear` | 清零崩溃统计（panic_count/last_crash） |
| `GET /api/coredump` | 下载 coredump 分区原始数据（ELF，octet-stream） |
| `POST /api/coredump/clear` | 清除 coredump（`esp_core_dump_image_erase`） |

**⚠ coredump 读取必须经 `OtaManager::runInInternalStack` + 内部 RAM 块缓冲**：
- 读 flash 分区（非 mmap）会 `spi_flash_disable_interrupts_caches_and_other_cpu`，coWorker 栈在 PSRAM = 崩
- `s_coredumpChunk[4096]` 静态 `.bss`（不加 `EXT_RAM_BSS_ATTR`）在内部 RAM，分块读入中转后拷贝 PSRAM 缓冲发送
- 下载前检查 `OtaManager::isUpdating()`（与 OTA 串行，避免插队）
- 分区第一个 4 字节 = 转储大小（`0xFFFFFFFF` = 空 → 404）

**解码崩溃现场**（PC 端）：
```bash
curl -o core.elf http://<主机名>.local/api/coredump
idf.py coredump-info -c core.elf build/The-Defect.elf
```

### 崩溃/复位原因（esp_reset_reason）

| 值 | 枚举 | 含义 |
|----|------|------|
| 1 | `ESP_RST_POWERON` | 上电 |
| 3 | `ESP_RST_SW` | 软件复位 `esp_restart()` |
| 4 | `ESP_RST_PANIC` | **panic 崩溃** |
| 5 | `ESP_RST_INT_WDT` | 中断看门狗 |
| 6 | `ESP_RST_TASK_WDT` | 任务看门狗 |
| 9 | `ESP_RST_BROWNOUT` | 欠压 |
| 其他 | — | 外部/深睡/SDIO/USB/JTAG 等（`resetReasonName` 映射表） |

> ⚠ v5.5.2 无 `esp_reset_reason_str()`（新版 API），用本地数字映射表。
> ⚠ 崩溃瞬间不写 FAT（flash 冲突），现场由 coredump 分区保存；panic 计数在下次启动时由 `detectCrash()` 记录。

---

## GUI 辅助类

**模块路径**: `main/gui/`

### 色板

| 名称 | 色值 | 用途 |
|------|------|------|
| `Color::BG` | `#1a1a2e` | 背景 |
| `Color::CARD` | `#2d2d3d` | 卡片背景 |
| `Color::PRIMARY` | `#0088ff` | 主色（按钮/强调） |
| `Color::SUCCESS` | `#00c853` | 成功/高电量 |
| `Color::WARNING` | `#ffa800` | 警告/中电量 |
| `Color::DANGER` | `#ff3b30` | 危险/低电量 |
| `Color::TEXT` | `#ffffff` | 正文 |
| `Color::SUBTLE` | `#888888` | 辅助文字 |

### 工厂方法

```cpp
class GUI {
    static void setBackground(lv_color_t color = Color::BG);
    static lv_obj_t* createPage(lv_obj_t* parent = nullptr);
    static lv_obj_t* createCard(lv_obj_t* parent, int32_t w, int32_t h);
    static lv_obj_t* createFlex(lv_obj_t* parent, lv_flex_flow_t flow, int32_t w = LV_SIZE_CONTENT, int32_t h = LV_SIZE_CONTENT);
    static lv_obj_t* createButton(lv_obj_t* parent, const char* text, int32_t w = 120, int32_t h = 40);
    static lv_obj_t* createTitle(lv_obj_t* parent, const char* text);
    static lv_obj_t* createSubtitle(lv_obj_t* parent, const char* text);
    static lv_obj_t* createValue(lv_obj_t* parent, const char* text);
    static lv_obj_t* createLabel(lv_obj_t* parent, const char* text);
    static lv_obj_t* createSwitch(lv_obj_t* parent);
    static lv_obj_t* createSlider(lv_obj_t* parent, int32_t w, int32_t min, int32_t max, int32_t init);
    static lv_obj_t* createProgressBar(lv_obj_t* parent, int32_t w, int32_t min, int32_t max, int32_t init);
    static lv_obj_t* createMetric(lv_obj_t* parent, const char* title, const char* value, const char* unit);
    static lv_obj_t* createProgressCard(lv_obj_t* parent, const char* title, int32_t min, int32_t max, int32_t init);
    static lv_obj_t* createMenuRow(lv_obj_t* parent, const char* text, lv_obj_t* rightWidget = nullptr);
    static lv_obj_t* createImage(lv_obj_t* parent, const void* src);
    static lv_obj_t* createImageFromFile(lv_obj_t* parent, const char* path);
};

static inline void styleCard(lv_obj_t* obj);
```

---

## 存储子系统

### FAT — Flash 分区

**模块路径**: `main/storage/fat.hpp`

| API | 说明 |
|------|------|
| `mountFlash()` | 挂载 fat 分区到 `/root/` |
| `unmountFlash()` | 卸载 |
| `formatFlash()` | 格式化分区 |
| `getSpace(&free, &total)` | 获取剩余/总空间 |
| `tree(path)` | 递归列出文件树 |
| `newFile(path)` / `removeFile(path)` | 文件 CRUD |
| `newFloor(path)` / `removeFloor(path)` | 目录 CRUD |

`FileBase` 虚基类，提供 `open()` / `close()` / `read()` / `write()` / `seek()` / `tell()` / `size()`。

### memFS — 内存文件系统

**模块路径**: `main/storage/mem.hpp`

| 参数 | 值 |
|------|-----|
| 挂载点 | `/root/mem` |
| 最大大小 | 6MB |
| 最大文件描述符 | 16 |

### SD — SD 卡

**模块路径**: `main/storage/sd.hpp`

`mountSd()` → 挂载到 `/root/sd`

### VFS 挂载点汇总

| 路径 | 存储介质 | 来源 |
|------|---------|------|
| `/root/` | FAT flash (16MB) | `mountFlash()` |
| `/root/mem` | PSRAM (6MB) | `mountMem()` |
| `/root/sd` | SD 卡 | `mountSd()` |

---

## PPA (Pixel Processing Accelerator) 配置

ESP32-P4 的 PPA 硬件加速 2D 渲染 (fill/blend/SRM)。当前配置与注意事项：

### sdkconfig

```
CONFIG_LV_USE_PPA=y                # 启用 PPA（fill/blend/SRM）
CONFIG_LV_USE_PPA_IMG=y            # PPA 加速图片渲染
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2
```

### 关键约束

| 参数 | 值 | 说明 |
|------|----|------|
| `CONFIG_LV_CACHE_DEF_SIZE` | 建议 ≥1MB | 图片缓存大小，避免每帧重复解码 JPEG |

> PPA fill/blend 与 SRM (Scale-Rotate-Mirror) 共享同一硬件引擎，大量 blend 操作会阻塞 SRM 旋转，导致帧率下降。实测中 PPA 加速 5 张卡片渲染时，blend 排队 + SRM 等待 → `ppa_do_scale_rotate_mirror` 耗时从 ~0.7ms 飙升至 ~15ms。

### 性能陷阱

| 问题 | 原因 | 解决 |
|------|------|------|
| `CONFIG_ESP_LVGL_ADAPTER_PARTIAL_AUX_IMG_CACHE` | 每帧末尾调用 `lv_image_cache_drop(NULL)` 清空整个缓存 | **禁用**该选项，直设 `LV_CACHE_DEF_SIZE` |
| LVGL 阴影 (`shadow_width`) | 模糊运算性能影响较大 | 适度使用，或改用预渲染方式 |
| 圆角 (`radius`) | 产生裁剪路径开销，但远小于阴影 | 可使用，避免过大的 radius 值 |

---

## Patches

**路径**: `patches/`

| Patch | 组件 | 文件 | 说明 |
|-------|------|------|------|
| `0001-esp_lvgl_adapter-frame-ready-callback.patch` | `espressif__esp_lvgl_adapter` | `display_bridge.h` / `lvgl_bridge_v9.c` / `display_manager.h/.c` | Bridge 帧完成回调 `on_frame_ready` → `display_manager_set_frame_ready_callback()`，供 ScreenStream 零拷贝采集 |
| `0002-lvgl__lvgl-ime-pinyin-fixes.patch` | `lvgl__lvgl` | `src/widgets/ime/lv_ime_pinyin.c` | **IME 修复合集**（原 `0005`–`0010` 合并）：i/u/v 过滤移除 / `count` uint16 / 空槽 HIDDEN + 选中重置 / 翻页焦点保持 / 残留 PRESSED·CHECKED 清除，见「LVGL 必要 patch」节 |
| `0003-esp_lv_decoder-disable-png.patch` | `espressif__esp_lv_decoder` | `esp_lv_decoder.c` / `esp_lv_decoder_config.h` | PNG 支持 `#if ESP_LV_ENABLE_PNG` 化（config.h 默认 1），主固件注入 `-DESP_LV_ENABLE_PNG=0` 关闭 → libpng + zlib 零引用裁掉（~100KB）；SPNG 同步禁用。本项目仅 JPG（硬件 JPEG），恢复 PNG 只需去掉 `main/CMakeLists.txt` 的注入 |

> 说明：PPA 卡死修复为 `esp_lvgl_adapter` 组件随包附带的官方补丁（非本目录补丁），见组件 README。

**使用**（跨平台 Python + `git apply`，无需 `patch` / `IDF_PATH`）:
```bash
python patches/apply.py          # 打所有补丁
python patches/apply.py --check  # 干跑报告（APPLICABLE / ALREADY-APPLIED / CONFLICT）
python patches/apply.py --revert # 还原所有
```
⚠ `idf.py reconfigure` 后需重新打补丁。
⚠ `git apply` **无 fuzz**：组件版本升级导致补丁上下文漂移时会报 CONFLICT（不静默错位），需按 `patches/README.md` 更新补丁。
⚠ 补丁目标目录显式登记在 `patches/apply.py` 的 `MANIFEST` 中，新增补丁须同步登记。

### 组件版本基线（补丁生成时）

lvgl **9.5.0** / esp_lvgl_adapter **0.4.3~1** / esp_lv_decoder 0.4.3 / esp_websocket_client 1.7.0 / idf **5.5.2**

### LVGL 构建选项覆盖（编译宏注入，非补丁）

部分 LVGL 构建选项（如 `LV_INV_BUF_SIZE`）**不是 Kconfig 符号**，无法用 menuconfig/sdkconfig 配置。
它们定义在 `lv_display_private.h` 等头文件中且有 `#ifndef` 保护 —— **编译期 `-D` 优先级高于头文件默认值**。

**覆盖方式**：`main/CMakeLists.txt` 中用 `idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)` +
`target_compile_definitions(${lvgl_lib} PUBLIC LV_INV_BUF_SIZE=256)` 注入编译宏
（与 `esp_lvgl_adapter` 给 LVGL 注入 `LV_CACHE_DEF_SIZE` 的官方姿势一致，见 `espressif__esp_lvgl_adapter/CMakeLists.txt`）。

**⚠ 规则**：
- **不要手动修改 `managed_components/`** —— 组件管理器更新/重装会被覆盖。
- 须用 `PUBLIC` 传播：`LV_INV_BUF_SIZE` 改变 `lv_display_t` 结构体布局（`inv_areas[LV_INV_BUF_SIZE]`），
  `lvgl__lvgl`、`esp_lvgl_adapter`（bridge 里有自己的 `inv_areas[LV_INV_BUF_SIZE]` 副本）与 `main` 必须一致，
  否则 ABI 不一致 → 内存踩踏。PUBLIC 沿链接接口传播可同时覆盖三者。

---

## Startup Flow (main.cpp)

```
Task::init(2)                           ← 协程调度器
srand(esp_random())                     ← 随机种子（60s 周期）

SystemMonitor::instance().setBrief(true) ← 系统监视器（CPU + 内存 + FPS）
SystemMonitor::instance().start(&display)
esp_event_loop_create_default()         ← 默认事件循环

// ── 存储 ──
mountFlash()                            ← FAT 分区 → /root/
mountMem()                              ← 内存文件系统 → /root/mem
mountSd()                               ← SD 卡 → /root/sd
nvsInit()                               ← NVS 初始化

// ── 日志重定向 + 崩溃检测（封壳设备远程调试） ──
LogService::instance().start()          ← esp_log_set_vprintf → FAT 文件 + /ws/log；早期缓冲落盘
LogService::instance().detectCrash()    ← 复位原因 + coredump 存在 → NVS 记录（panic 计数）

// ── ELF 插件 SDK ──
elf_sdk_init()                          ← 注册符号表 g_main_elfsyms（任何 dlopen 之前）

// ── C6 协处理器（与显示初始化并行） ──
Slave::instance().start()               ← 后台 Task SDIO 复位 (~1.5s)

// ── 显示 ──
Display::init()                         ← LVGL 适配器
ILI9881c::init(720, 1280, fbCount)      ← MIPI DSI 面板
display.bindDisplay()                   ← 绑定面板
Touch::init(iic, ...)                   ← GT911 触摸（可选）
GT911Indev::instance().start(...)       ← 五 indev 多指桥接（取代 bindTouch）
display.start()                         ← LVGL worker task

// ── 屏幕串流 ──
ScreenStream::instance().start(&display, 720, 1280)

// ── 字体 ──
FontLoader::load("F:system/NotoSC.ttf", 24)   ← Small
FontLoader::load("F:system/NotoSC.ttf", 32)   ← Medium (Default)
FontLoader::load("F:system/NotoSC.ttf", 56)   ← Large
// 字体回退链
const_cast...->fallback = lv_font_get_default();

// ── 虚拟输入 ──
VirtualIndev::instance().start(&display)

// ── 音频 ──
ES8311::init(iic, config)               ← 编解码器初始化
Audio::instance().init(audio)           ← 音频管理器
Audio::loadVolumeFromNvs()              ← 音量恢复

// ── 电池 ──
BatteryManager::instance().init()

// ── App 导航系统（系统级单例，程序运行期常驻） ──
AppStackManager::instance().init(&display)          ← 注入 Display（单线程阶段）
display.setStackManager(&AppStackManager::instance())
AppStackManager::instance().createStack()           ← 根栈（Stack 0）
AppStackManager::instance().push(new DesktopApp(&display))  ← 桌面应用

// ── BLE 手柄 ──
BleGamepad::instance().start(&display)

// ── AI 助手浮动 Overlay ──
AiAgentOverlay::instance().start(&display)

// ── 低电量检测提醒（主机+手柄） ──
BatteryMonitor::instance().start(&display)

// ── 网络 ──
wifiInit(true)                          ← 初始化（通过 C6）
wifiStart()
wifiStationStart(); wifiConnect(ssid, pass)
if (!connected) wifiApStart()           ← 连接失败 → AP 模式
mdnsInit(); mdnsStart(hostname, ...)
mdnsServiceAdd("_http", 80)
mdnsServiceAdd("_ws", 8080)
mdnsServiceAdd("_p4console", 80)       ← 设备发现专用服务

// ── 服务器 ──
OtaManager::instance()                  ← 单例构造：内部 RAM 栈任务 + 队列（单线程阶段，HTTP 并发前）
Task::addTask(延迟 10s → checkRollback) ← OTA 启动检查：回滚判定 + 状态日志（注册点在启动流程末尾）
serverStart()                           ← HTTP 端口 80
wsServerStart()                         ← WebSocket 端口 8080
wsServerRegisterWs("/ws/log", logWsHandler) ← 实时日志推送（诊断页订阅）
udpInputStart()                         ← UDP 端口 8082（P4→P4 手柄透传）

// ⚠ 无主循环：app_main 至此返回（所有后台工作由独立 Task 承担：LVGL worker /
//   BLE / HTTP 6 worker / WS / deleteApp / 协程调度 daemon）。因此关键对象
//   （AppStackManager 等）必须用单例/常驻存储，绝不能用局部对象——返回时析构
//   会在后台任务并发运行中删对象 → 竞态崩溃。
```

agent 应查看 `main/main.cpp` 的最新代码为准。

---

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
