# main 代码文件介绍索引

> 用途：为 main/ 下每个代码文件提供路径 + 功能说明，供脚本解析与后续开发参考。
> 覆盖范围：main/ 下全部 `.cpp / .hpp / .h / .c / .inl / .inc` 共 **180** 个代码文件。
> 解析规则：每个条目以 `## FILE: <路径>` 开头，紧跟一行 `功能：<描述>`。
> 脚本可用正则 `^## FILE: (.*)$` 提取路径、`^功能：` 提取描述；条目之间以空行分隔。

---

## FILE: main/main.cpp
功能：程序入口 `app_main`，按序初始化协程调度器、存储（FAT/memFS/SD/NVS）、C6 协处理器、显示（ILI9881c+LVGL）、触摸、屏幕串流、字体、虚拟输入、音频、电池；随后创建 AppStackManager 根栈并推入 DesktopApp，启动 BLE 手柄与 AI 助手 Overlay，最后开启 WiFi/mDNS/HTTP/WebSocket/UDP 网络服务并进入主循环。

## FILE: main/task/task.hpp
功能：声明 `Task` 协程调度器接口（`Affinity`/`Priority` 常量、`Function_t` 回调约定、`addTask`/`removeTask`/`setAffinity`/`dumpTask`）和 `Thread` 轻量线程封装（移动语义、静态栈、suspend/resume），所有模块通过它注册周期回调。

## FILE: main/task/task.cpp
功能：实现 `Task` 轮询式协程调度器（`init` 创建多个 daemon 线程、`addTask`/`removeTask` 注册删除任务、`daemonMain` 按 `nextCallTick` 循环执行并支持 affinity 亲和性），以及 `Thread` 静态 FreeRTOS 线程封装（构造创建、析构延时回收栈、suspend/resume）；这是全项目（如 LVGL worker、手柄 dispatch、CPU 监视器等）共用的任务调度基础设施。

## FILE: main/mutex/mutex.hpp
功能：基于 FreeRTOS `xSemaphoreCreateMutex` 的 `Mutex` 非递归互斥锁封装（try_lock/lock/unlock，析构确保无人持有），并提供 RAII 的 `Lock` 守卫类；用于 BleGamepad、Task 链表等模块的多线程保护。

## FILE: main/gpio/gpio.hpp
功能：声明 `GPIO` 引脚装饰器封装，重载 `=` 写电平、`bool`/`GPIO_NUM`/`int` 读引脚，支持 Mode/Pull/Interrupt 配置与 ISR 注册/注销及全局中断服务安装，是 IIC、电池充电检测、SD 电源控制等所有 GPIO 操作的统一入口。

## FILE: main/iic/iic.hpp
功能：声明 `IIC`（I²C 主控总线，可移动不可拷贝，`IicPortAuto` 自动端口）和 `IICDevice`（7 位地址设备句柄，收发/请求接口）两个类的接口，作为触摸与音频等外设的底层 I²C 抽象。

## FILE: main/iic/iic.cpp
功能：实现 `IIC` 主控总线（新建/删除 I²C master bus、`detect` 探测设备地址）和 `IICDevice` 设备封装（添加/移除设备、`transmit`/`receive`/`request` 读写），内部开启内部上拉并自动端口选择；GT911 触摸、ES8311 音频编解码器均依赖此模块通信。

## FILE: main/gui/gui.hpp
功能：声明 `GUI` 类及其内嵌 `Color` 色板（BG/CARD/PRIMARY/SUCCESS/WARNING/DANGER/TEXT/SUBTLE）和全部工厂方法原型，并附带 `styleCard` 内联函数定义卡片通用样式，供各 App 快速搭建一致风格的 LVGL 界面。

## FILE: main/gui/gui.cpp
功能：实现 `GUI` 的 14 个 LVGL 工厂方法（createPage/Card/Flex/Button/Title/Switch/Slider/ProgressBar/Metric/ProgressCard/MenuRow/Image 等），统一设置尺寸、配色、圆角、Flex 布局，并内置 `createMetric`/`createProgressCard` 等组合控件；是桌面、设置、游戏房间等所有 App 界面的公共构建工具。

## FILE: main/utility/nonCopyAble.hpp
功能：定义 `NonCopyAble` 基类，删除拷贝构造/赋值、保留移动语义，禁止对象被复制；被 FileBase、memFS 内存块链表等资源持有类继承，防止意外拷贝导致句柄/指针重复释放。

## FILE: main/utility/stringCompare.hpp
功能：声明 `prefixCompare` 和 `stringCompare` 两个无 STL 依赖的定长字符串比较函数原型，供 memFS 等模块包含使用。

## FILE: main/utility/stringCompare.cpp
功能：实现 `prefixCompare`（按长度比较前缀是否匹配）与 `stringCompare`（等长逐字符比较）两个轻量字符串比较函数，供 memFS 文件树查找等路径解析场景使用。

## FILE: main/storage/vfs.hpp
功能：定义三个 VFS 挂载点前缀常量（`PerfixRoot="/root"`、`PerfixMem="/root/mem"`、`PrefixSd="/root/sd"`）及 `FileType` 枚举（File/Floor/Both），是 FAT、memFS、SD 与各应用共享的路径约定。

## FILE: main/storage/fat.hpp
功能：声明 FAT 分区常量（`FlashPartitionLabel="fat"`、最大 16 个文件）及挂载/格式化/空间查询/文件目录 CRUD 等 C 风格函数，并声明 `FileBase`（带偏移/大小管理的虚基类）、`IFile`/`OFile`/`IOFile` 读写流和 `Floor` 目录遍历类的接口。

## FILE: main/storage/fat.cpp
功能：实现 FAT 分区挂载（`mountFlash` 用 `esp_vfs_fat_spiflash_mount_rw_wl` 挂载到 /root）、卸载、格式化、空间查询与 `tree` 递归列目录，并提供 `newFile`/`removeFile`/`newFloor`/`removeFloor` 等文件/目录 CRUD 和 `FileBase`/`IFile`/`OFile`/`Floor` 文件与目录流封装；是 /root 下所有资源（lua 脚本、系统字体、市场文件）的存储基础。

## FILE: main/storage/mem.hpp
功能：声明 memFS 的配置常量（最大 6MB、16 个文件描述符、512 字节数据块、错误码枚举）与 `mountMem()` 接口，供 VFS 层集成及上层使用 /root/mem 内存文件系统。

## FILE: main/storage/mem.cpp
功能：实现基于 PSRAM 的 6MB 内存文件系统（memFS），用链表式 `MemFileHead`/`MemFileFloorHead`/`MemFileDataBlock` 组织目录树与数据块，并通过 `memOpen`/`memRead`/`memWrite`/`memSeek` 等一组 VFS 回调接入 ESP VFS 层，挂载到 /root/mem 提供高速临时文件存储。

## FILE: main/storage/sd.hpp
功能：声明 `mountSd()` 与 `unmountSd()` 两个 SD 卡挂载/卸载接口，挂载点为 /root/sd。

## FILE: main/storage/sd.cpp
功能：实现 `mountSd`/`unmountSd`：先检查 /root/sd 目录存在并对 GPIO45 做 SD 电源重启，再配置 SDMMC 4-bit 高速模式（P4 使用片上 LDO 供电）通过 `esp_vfs_fat_sdmmc_mount` 挂载 SD 卡到 /root/sd，供大容量媒体/资源存储。

## FILE: main/monitor/cpuMonitor.hpp
功能：声明 `CpuMonitor` 单例类（start/stop/setBrief），维护前后两次任务快照、匹配标记与 IDLE 计数器缓冲区，描述通过 FreeRTOS run time counter 差分统计各任务 CPU 占用率的接口。

## FILE: main/monitor/cpuMonitor.cpp
功能：实现 `CpuMonitor` 单例：注册周期 pollTask（默认 1s）通过 `uxTaskGetSystemState` 差分采样计算各任务及双核占用率并输出串口（支持仅输出总结行的简略模式，用双缓冲避免每轮 malloc），供开发期性能分析。

## FILE: main/power/powerManager.hpp
功能：声明 `PowerManager` 单例接口（prepareShutdown/shutdown/isWakeupBoot/getWakeupReasonStr），内部定义 RTC 持久化的 `RtcData`（magic/bootCount/shutdownReason），统一管理关机流程。

## FILE: main/power/powerManager.cpp
功能：实现 `PowerManager` 单例：构造时用 `esp_sleep_get_wakeup_causes` 检测冷启动/Deep-sleep 唤醒，`prepareShutdown` 记录关机原因到 RTC memory（RTC_DATA_ATTR），`shutdown` 调用 `esp_deep_sleep_start` 进入 Deep-sleep 靠硬件复位唤醒，被 PowerManagementApp 与 AI power_off 动作调用。

## FILE: main/battery/batteryManager.hpp
功能：声明 `BatteryManager` 单例类（init/deinit/getPercent/getVoltageMv/isCharging + 静态 getIcon/getColor/startChargingAnim/stopChargingAnim），封装 ADC 通道、分压电阻与充电检测 GPIO 配置及 adc_battery_estimation 组件句柄。

## FILE: main/battery/batteryManager.cpp
功能：实现 `BatteryManager` 单例：创建自有 ADC oneshot/校准句柄并以 external 模式注入 `adc_battery_estimation` 组件，提供 `getPercent`（电量百分比）、`getVoltageMv`（按 5.1k/10.2k 分压还原电压）、`isCharging`（GPIO23 低电平=充电），以及电池格数 ping-pong 呼吸动画和 `getIcon`/`getColor`（绿/黄/红）静态显示辅助；供桌面状态栏与电源管理 App 显示电量。

## FILE: main/display/display.hpp
功能：`Display` 类头文件，声明 LVGL 适配层核心接口：`init`/`bindDisplay`/`bindTouch`/`start`/`getLvglDisplay`，提供可嵌套的 `LockGuard` 锁守卫（RAII，`lockGuard()` 便捷方法）、FPS 统计、`applyApp()` 前台切换，并持有 `AppStackManager` 与当前活跃 App 指针，是整个 LVGL 图形栈的入口封装。

## FILE: main/display/display.cpp
功能：`Display` 类的实现：初始化 esp_lv_adapter 并注册 720×1280 MIPI 显示设备（PPA 关闭、双缓冲）、绑定 GT911 触摸，`applyApp()` 持锁切换前台 App 的 LVGL 屏，背光亮度统一委托给 `ILI9881c` 单例（`setBrightness`/`getBrightness`/NVS 持久化），并实现 `LockGuard` 递归锁 RAII 供各 App 线程安全访问 LVGL。

## FILE: main/display/font.hpp
功能：`FontLoader` 类头文件，声明三档字号枚举（Small=24/Medium=32 默认/Large=56）、静态字体加载与默认字体查询/设置接口，内部以最多 3 个槽位的 `FontEntry` 数组（EXT_RAM 静态存储）缓存已加载字体句柄。

## FILE: main/display/font.cpp
功能：`FontLoader` 的实现：注册 LVGL VFS 驱动器 'F'（将 `F:system/NotoSC.ttf` 映射到 FAT 分区 `/root/system/...`），用 FreeType（`esp_lv_adapter_ft_font`）加载全局默认字体，并按 Small/Medium/Large 三档字号缓存到 `s_fonts[]`，提供 `load`/`getDefault`/`setDefault`。

## FILE: main/display/ili9881c.hpp
功能：`ILI9881c` 单例类头文件，声明 MIPI DSI 面板初始化（`init`/`deinit`）、面板/IO 句柄获取、背光亮度控制（`setBrightness`/NVS 存取）接口，并集中定义硬件参数（2 数据通道、920Mbps、RESET=GPIO27、背光=GPIO26、LDO3 2.5V 供电）。

## FILE: main/display/ili9881c.cpp
功能：ILI9881C 面板驱动的实现：配置 MIPI DSI 总线/DBI panel IO/DPI 面板（2 数据通道 920Mbps、62MHz DCLK、DMA2D），下发 `initCommand.inl` 初始化序列并开机点亮，负责背光 PWM 控制——LEDC 10-bit + gamma 2.2 查找表线性插值映射到驱动电压窗口（duty 230~450），亮度经 NVS 持久化/恢复。

## FILE: main/display/initCommand.inl
功能：ILI9881C 厂商初始化命令序列数据文件（被 ili9881c.cpp 内联包含），以 `ili9881c_lcd_init_cmd_t` 数组形式定义 4 个页（Page3/4/1/0）约 200 条寄存器命令，含 Gamma 正负校正表，末尾为 Sleep Out（延时 120ms）与 Display On（延时 20ms）。

## FILE: main/touch/touch.hpp
功能：`Touch` 类头文件，声明 GT911 I²C 触摸封装接口：构造（复用 `IIC` 总线 + GPIO 复位/中断）、`getHandle`（供 `Display::bindTouch` 使用）、`update`/`getCount`/`operator[]`，定义 720×1280 分辨率与最多 5 点触控等常量。

## FILE: main/touch/touch.cpp
功能：`Touch` 类的实现：通过 I²C 总线创建 GT911 面板 IO 并注册 esp_lcd_touch 触摸设备（swap_xy + mirror_x 坐标校正、720×1280、复位/中断引脚），提供 `update()` 读取触控点、`getCount()` 与 `operator[]` 获取触点数据，支持移动语义与 RAII 析构释放。

## FILE: main/virtualIndev/virtualIndev.hpp
功能：`VirtualIndev` 单例类头文件，声明虚拟触摸输入设备接口：`start(Display*)` 注册进 LVGL、`sendTouch(state, point)` 注入触摸事件，内部保存单份 `Sample`（状态/坐标/新数据标志）+ Mutex，供 `/ws/touch` Web 客户端远程触摸注入用。

## FILE: main/virtualIndev/virtualIndev.cpp
功能：`VirtualIndev` 的实现：注册一个 LVGL POINTER 类型输入设备，`sendTouch()` 供 Web 端注入触摸并做服务端去重（坐标+状态未变且未消费则跳过），`indevReadCb` 使用 `hasNewData` + `continue_reading` 机制让 LVGL 有新事件立即重读、空闲时维持按压状态，`Mutex` 保护样本读写。

## FILE: main/virtualIndev/wsTouchHandler.hpp
功能：`wsTouchHandler` 头文件，声明 `/ws/touch` WebSocket 入口函数，文档化 12 字节触摸帧协议（x/y/type），供 wsServer 在 `wsServerStart()` 后注册使用。

## FILE: main/virtualIndev/wsTouchHandler.cpp
功能：`/ws/touch` WebSocket handler 实现：握手后接收 12 字节二进制帧（3×int32 小端：x/y/type，type 2=抬起其余=按下），PSRAM 分配缓冲解析后调用 `VirtualIndev::sendTouch()` 注入 LVGL，实现 Web 端远程触摸操作屏幕。

## FILE: main/screenStream/screenStream.hpp
功能：`ScreenStream` 单例类头文件，声明屏幕串流模块接口：`start`（注册 frame-ready 回调 + 初始化硬件 JPEG 引擎）/`stop`/`captureJpeg`/`isStarted`，内部缓存帧缓冲指针与 JPEG 编码器句柄，是 `/ws/stream` Web 屏幕查看器的数据源。

## FILE: main/screenStream/screenStream.cpp
功能：`ScreenStream` 的实现：通过 patch 后的 bridge `frame_ready` 回调缓存每帧完成后的帧缓冲指针（零额外渲染/零拷贝），`captureJpeg()` 用 ESP32-P4 硬件 JPEG 编码器（RGB565→YUV420、质量 60）将缓存帧直接编码为 JPEG，供 MJPEG 屏幕串流使用。

## FILE: main/screenStream/wsStreamHandler.hpp
功能：`wsStreamHandler` 头文件，声明 `/ws/stream` 屏幕串流（MJPEG）WebSocket 入口函数，说明客户端连接后发任意 binary 帧即触发双任务并行编码+发送，供 wsServer 注册。

## FILE: main/screenStream/wsStreamHandler.cpp
功能：`/ws/stream` WebSocket handler 实现：支持客户端切换（新连接先异步踢掉旧客户端），数据帧到达后启动"编码任务 + 发送任务"双任务——编码任务调 `ScreenStream::captureJpeg()` 入双缓冲队列，发送任务手工构造 WS 二进制帧头（0x82）直接 `send()`，连续 50 次错误自动断开。

## FILE: main/audio/Audio.hpp
功能：音频模块头文件，声明 `Audio`（Meyer's Singleton 音频管理器：init/deinit/主音量/play 文件或 AudioSample/usedStreamCount/detectFormat）、`AudioHandle`（仿 `std::thread` 生命周期的播放句柄值类型，支持 loop/volume/play/stop/detach/pause/resume）与 `AudioSample`（预解码 PCM 缓存，activeCount 限流）三类接口。

## FILE: main/audio/Audio.cpp
功能：音频子系统的实现：`AudioHandle` 解码任务负责从 FAT 文件（MP3/AAC/FLAC/Opus 等）解码 PCM 并写入 render 多流混音器（支持暂停/恢复/循环/音量/自适应限速），`AudioSample::load()` 将文件预解码重采样到 48kHz 存入 PSRAM 供零延迟播放，`Audio` 单例初始化 GMF 池 + render（14 流上限），通过 OPENED/CLOSED 事件自动开关 ES8311 并恢复主音量，主音量 NVS 持久化。

## FILE: main/audio/ES8311.hpp
功能：`ES8311` 类头文件，声明基于 esp_codec_dev 的编解码器封装接口：`Config`（I²S MCLK/BCK/WS/DOUT 引脚、PA 使能、I²C 地址）、`init`/`deinit`、流式 `open`/`write`/`close` 与一次性 `play`、音量/静音控制，供 `Audio` 管理器底层驱动调用。

## FILE: main/audio/ES8311.cpp
功能：ES8311 编解码器驱动的实现：初始化 I²S TX 通道（Philips 16bit 立体声）与 esp_codec_dev 设备（I²C 控制 + GPIO 功放使能，DAC 仅播放模式），提供 `open`/`write`/`close` 流式播放、`play` 一次性播放、`setVolume`/`getVolume`/`setMute` 音量与静音控制。

## FILE: main/gamepad/gamepadState.hpp
功能：定义手柄协议的数据结构：`GamepadButton` 位图（与 C6 固件 HID 报告对齐）、`GamepadState`（按钮+双摇杆+扳机+dpad，含 `isPressed`/判等）、扫描结果 `ScanDevice`、连接上下文 `DeviceContext` 及输入队列消息 `GamepadInputEvent`，是 BLE 输入源与 VirtualGamepad/游戏 App 之间的共用类型。

## FILE: main/gamepad/bleGamepad.hpp
功能：声明 `BleGamepad` 单例类（配合 `main/app/bleSettingsApp/` 扫描 UI），提供 start/stop、startScan/stopScan、connect/disconnect、NVS 配对读写、电量查询等接口，内部持有 DeviceContext[4] 设备表、输入队列、NimBLE GAP 回调（hostTask/processTask 任务入口）。

## FILE: main/gamepad/bleGamepad.cpp
功能：实现 BLE 手柄模块：通过 esp_hosted 经 ESP32-C6 协处理器启用 NimBLE 控制器，管理扫描（HID 过滤 0x1812/appearance）、连接/断开、4 玩家槽位分配与 NVS 配对持久化，processTask 从输入队列取事件后调用 `VirtualGamepad::inject(0,...)` 注入统一输入抽象层（不再直连 App），并支持启动 5 秒自动重连已配对设备。

## FILE: main/gamepad/virtualGamepad.hpp
功能：声明 `VirtualGamepad` 系统层单例：`inject(hostId, playerId, state)` 汇集本地 BLE/远程 UDP/WS 输入源，提供掩码控制 `setHostMask/setAllMasks`、`resetPlayer`、`getState` 及从机模式转发 `startForwarder/stopForwarder`，是跨主机联机输入分发与掩码隔离的核心入口。

## FILE: main/gamepad/virtualGamepad.cpp
功能：实现统一手柄输入抽象层：dispatchTask 以固定 60fps 遍历 `m_hostMask` 启用的 (hostId, playerId) 全量分发到前台 App（AI Overlay 可见时跳过）；forwarderLoop 在独立线程按反码掩码把本机输入打包为 11 字节帧经 UDP 8082 转发到远程主机，并带 `m_lastForwarded` 去重。

## FILE: main/aiAgent/aiProtocol.hpp
功能：声明 AI Agent 通信协议类型：`AiActionType` 枚举（respond_text/open_app/select/activate/navigate_back/set_brightness/play_audio/wifi_connect/system_info/power_off/generate_lua_game/read_file 等）、`AiAction` 统一动作结构（含 cJSON params 的深拷贝资源管理）、`AiResponse` 解析结果（actions + memory + nextRound）。

## FILE: main/aiAgent/aiConfig.hpp
功能：声明 `AiConfig` 结构与加载函数：从 FAT 分区 `/root/system/ai_config.json` 读取 api_url/model/temperature/max_tokens/timeout_ms，从 NVS（`ai_agent` 命名空间 `ai_api_key`）读取 API Key，`aiConfigLoad()` 汇总并置 valid 标志，`aiConfigSaveApiKey()` 持久化密钥。

## FILE: main/aiAgent/aiClient.hpp
功能：声明 `HttpClient` 及回调类型，实现 OpenAI 兼容 API 的 HTTPS 调用：拼装 system/history/user 消息 JSON，在后台 Task 中用 esp_http_client open/write/fetch_headers 模式 POST `/v1/chat/completions`，PSRAM 动态扩容读取 chunked 响应（支持 90s 长超时、crt_bundle 校验），结果经回调返回给 AiAgentOverlay。

## FILE: main/aiAgent/aiParser.hpp
功能：声明 `AiParser` 解析器：解析 OpenAI Chat Completion 响应，鲁棒处理 markdown 包裹、最外层 `{}` 查找、tool_calls Function Calling 降级、裸数组包裹，逐个解析 action 对象为 `AiAction`，并提取顶层 `memory`（长期记忆）与 `nextRound`（多轮协作标志），非法 JSON 生成 ParseError 动作。

## FILE: main/aiAgent/aiExecutor.hpp
功能：声明 `AiExecutor` 执行器：顺序执行 AiAction 列表，先让前台 App 的 `executeAiAction()` 优先拦截，再按类型兜底处理 Select/Activate/NavigateBack/NavigateHome/OpenApp/SetBrightness/SetVolume/PlayAudio/WifiConnect/SystemInfo/PowerOff/ReadFile/GenerateLuaGame 等系统动作，并汇总成功/失败明细供 UI 与 AI 上下文反馈。

## FILE: main/aiAgent/aiSystemInfo.hpp
功能：提供 `collectSystemInfo()` 内联函数，采集设备状态供 AI 分析：电池电量/充电、WiFi/IP/AP SSID、FAT 存储空间、PSRAM/内部 RAM 空闲、运行时长、已连接手柄及各自电量，格式化为多行文本，供 AiExecutor 与 AiAgentOverlay 共用。

## FILE: main/lua/luaRuntime.hpp
功能：声明 `LuaRuntime` 类：封装 `lua_State*` 生命周期（禁拷贝），接口含 init（沙箱化开库）、loadScript/loadString、callFunction、checkError、getLastError、栈值弹出与 `registerCFunction`，每个 LuaGameApp 持独立实例实现游戏间状态隔离。

## FILE: main/lua/luaRuntime.cpp
功能：实现 `LuaRuntime` RAII 封装：`lua_newstate` 创建 VM 并在析构 `lua_close`；`init()` 用 `luaL_openselectedlibs` 只开安全库（base/coroutine/math/string/table/utf8）并置空 dofile/loadfile 实现沙箱；提供 loadScript/loadString/callFunction/hasFunction、错误信息保存与栈弹出（popString/popBool/popInteger）等操作。

## FILE: main/lua/luaModules.hpp
功能：声明 `luaRegisterModules(lua_State*)` 注册函数，统一把各 luaopen 绑定模块（display/input/audio/thread/storage）注入指定 Lua 状态机，作为 Lua 沙箱引擎的模块装配入口。

## FILE: main/lua/luaModules.cpp
功能：实现 `luaRegisterModules(lua_State*)`：通过 luaL_requiref 注册 display/input/audio/thread/storage 五个自定义绑定模块到 VM，并用 esp_random() 为 math.randomseed 设置随机种子，供 LuaRuntime::init 与 LuaGameApp 启动时调用。

## FILE: main/lua/bindings/luaDisplay.hpp
功能：声明 `LuaDisplayContext`（canvas/screen/Display 指针，经 lua_upvalue 注入每个 display API）与 `luaopen_display` 入口，是 Lua 绘图函数与 LVGL/Display 适配层之间的上下文桥。

## FILE: main/lua/bindings/luaDisplay.cpp
功能：实现 Lua display 绑定模块：支持 `"#ff0000"/0xff0000/{r,g,b}` 三种颜色解析，提供 fill_screen/fill_rect/draw_rect/draw_text/draw_line/draw_circle/width/height 简化帧缓冲绘图 API，内部经 Lua registry 取 `LuaDisplayContext` 并通过 `Display::lockGuard()` 持 LVGL 锁在 canvas 上创建对象绘制。

## FILE: main/lua/bindings/luaInput.hpp
功能：声明 `LuaInputContext`（当前帧/上一帧 GamepadState 快照数组与手柄数，LuaGameApp 每帧循环前更新）及 `luaopen_input` 入口，桥接 Lua 脚本与统一手柄输入层。

## FILE: main/lua/bindings/luaInput.cpp
功能：实现 Lua input 绑定模块：通过按钮名映射表（大小写不敏感）提供 `is_pressed`/`is_just_pressed`（含 D-pad 方向与边沿检测）、`joystick(left/right)` 返回 {x,y}、`buttons` 位图、`connected`/`player_count` 查询，从 registry 取 `LuaInputContext` 读取每帧快照。

## FILE: main/lua/bindings/luaAudio.cpp
功能：实现 Lua audio 绑定模块：`play(path)`（一次性）、`play_bgm(path)`（循环）、`set_volume(0-100)` 委托到 `Audio::play/setMasterVolume`（handle detach 生命周期），`stop_bgm` 留空待扩展，供 Lua 游戏播放音效/背景乐。

## FILE: main/lua/bindings/luaStorage.hpp
功能：声明 `luaopen_storage` 入口函数，供 luaRegisterModules 注册安全存档模块（save/load/remove/exists），是 Lua 沙箱中唯一文件读写通道。

## FILE: main/lua/bindings/luaStorage.cpp
功能：实现 Lua storage 绑定模块（沙箱白名单存档）：`save/load/remove/exists` 限定 `/root/lua/data/` 目录，`sanitizeName` 校验文件名只允许字母数字下划线点连字符并杜绝路径穿越，单文件 ≤256KB，防止脚本越权访问系统文件。

## FILE: main/lua/bindings/luaThread.cpp
功能：实现 Lua thread 绑定模块：`sleep(ms)`（vTaskDelay）、`now()`（esp_timer 毫秒时间戳）、`random(min,max)`（esp_random 范围随机），为 Lua 游戏提供延时/计时/随机能力。

## FILE: main/market/marketClient.hpp
功能：声明 `MarketClient` 及 `MarketScript` 数据结构，封装游戏市场 HTTP 客户端：异步（Task 中执行）实现 GET `/api/scripts` 列表、下载脚本文本、下载截图文件及 POST `/api/stats/download` 上报，内部提供 URL 拼接与 PSRAM 动态扩容的 httpGetBody/httpPostJson 阻塞工具，结果经回调通知 MarketApp。

## FILE: main/market/marketConfig.hpp
功能：声明 `MarketConfig` 结构与加载函数：从 FAT 分区 `/root/system/market_config.json` 读取 `server_url` 与 `timeout_ms`，`marketConfigLoad()`/`marketConfigGetUrl()` 提供市场服务器基地址（去尾斜杠），供 MarketClient 使用。

## FILE: main/wifi/wifi.hpp
功能：声明 WiFi 双模（STA/AP + NAT）全部 C 风格接口，包括 wifiInit/Start/Stop、wifiStationStart/Scan/Connect/GetIp、wifiApStart/Set/Stop、wifiNatSetAutoStart/Start/Stop，供 serverKernal 与 WifiSettingsApp 调用。

## FILE: main/wifi/wifi.cpp
功能：实现 C 风格 WiFi 双模 API：初始化/销毁 STA 与 AP 网卡（支持通过 C6 的 esp_wifi_remote 初始化），事件处理（断连重试、获取 IP），STA 扫描/连接/断开，AP 启动配置，以及 STA→AP 的 NAT 转发；全局状态位（wifiStarted/StationStarted/Connected/NatStarted）供 HTTP 服务器与 WiFi 设置界面查询。

## FILE: main/wifi/slave.hpp
功能：声明 C6 协处理器（esp_hosted SDIO 从机）管理单例 Slave，提供 start()/waitReady()/isReady() 接口，内部用 EventGroup 位标志实现可重入、多 Task 并发的就绪等待。

## FILE: main/wifi/slave.cpp
功能：实现 C6 协处理器管理单例：start() 在后台 Task 中早期调用 esp_hosted_connect_to_slave() 与显示初始化并行，waitReady()/isReady() 通过 EventGroup(kReadyBit) 支持多任务阻塞等待，BLE/WiFi 模块据此等待 C6 就绪。

## FILE: main/wifi/socketStream.hpp
功能：声明 SocketStreamBase（socket 句柄 + 有效性）、ISocketStream 输入流、OSocketStream 输出流、IOSocketStream 双向流四个类，定义 64 字节缓冲大小与 socket 类型别名。

## FILE: main/wifi/socketStream.cpp
功能：实现 TCP 套接字流封装：ISocketStream 带 64 字节输入缓冲的 get/peek/read/getline/ignore 读取，OSocketStream 带输出缓冲的 put/write/sendNow（满缓冲自动刷出），IOSocketStream 双向组合，供 HTTP 服务器逐行解析请求。

## FILE: main/wifi/socketStreamWindow.hpp
功能：声明 SocketStreamWindow 连接池窗口类，包含 IOSocketStream、独占 Mutex 与静态共享 Mutex/enabledCount，提供 setSocket/check/enable/disable/closeSocket/getEnabledCount 接口。

## FILE: main/wifi/socketStreamWindow.cpp
功能：实现连接池窗口 SocketStreamWindow：setSocket 占用窗口并累计 enabledCount，enable/disable 用 Mutex try_lock 保护窗口独占，closeSocket 关闭连接释放窗口；HTTP 服务器 6 个 worker 用它轮询分配连接。

## FILE: main/wifi/http.hpp
功能：声明 HTTP 协议基础类型：HttpMethod/HttpStatus/HttpContentType 枚举、HttpStringPair/HttpPairs/HttpCookie/HttpCookies/HttpHeads、HttpBase/HttpRequest/HttpRespond 类及各转换函数，是裸 socket HTTP 服务器的协议层。

## FILE: main/wifi/http.cpp
功能：实现 HTTP 协议解析：字符串比较与 stoa/atos 转换工具，HttpStringPair/HttpPairs 动态键值对容器，HttpCookie/HttpCookies 的 Cookie 收发（含 Max-Age/Path），HttpHeads 从流接收/发送请求头，并定义 HttpRequest/HttpRespond 的 receive/send、Method/Status/ContentType 枚举转换。

## FILE: main/wifi/mdns.hpp
功能：声明 mDNS 服务接口：mdnsInit/Deinit/Start/ServiceAdd、mdnsGetHostname/mdnsGetConfiguredHostname（NVS 读取）与 mdnsSetHostname（校验+持久化+热重启）。

## FILE: main/wifi/mdns.cpp
功能：实现 mDNS 封装：mdnsInit/Deinit/Start 设置主机名与实例名，mdnsServiceAdd 注册 _http(80)/_ws(8080)/_p4console(80) 服务并存入注册表，mdnsSetHostname 校验并持久化到 NVS 后热重启 mDNS 重注册全部服务，供设备发现与主机名自定义。

## FILE: main/wifi/nvs.hpp
功能：内联实现 nvsInit()：初始化 NVS 闪存，遇到无空闲页/新版本错误时先擦除再初始化，是 WiFi 配置、手柄配对、主机名等持久化数据的底层入口。

## FILE: main/server/serverKernal.hpp
功能：声明 HTTP 服务器生命周期接口 serverStart(maxAutoRestartTimes=3)/serverStop/serverIsStarted，供 main.cpp 启动、失败自动重启使用。

## FILE: main/server/serverKernal.cpp
功能：实现端口 80 的裸 socket TCP HTTP 服务器：主 Task accept + 6 个 worker（SocketStreamWindow 连接池）处理 GET/POST/PUT/DELETE，支持静态文件（/root/server）、文件管理页、/api 系列（aiConfig/aiTest/mdnsConfig/wifiScan/wifiStart/apStart/formatFlash/systemRestart/getSpace/floor），并关联 AI 配置与 mDNS 设置。

## FILE: main/server/buildinHtml/file.hpp
功能：定义内置文件管理网页（HtmlFile 常量，HTML+JS）：浏览目录、上传/删除文件、新建文件夹、显示剩余空间，通过 /file 与 /api/floor、/api/getSpace 等接口管理 FAT 分区。

## FILE: main/websocket/wsClient.hpp
功能：声明 WsClient 类：connect/disconnect/isConnected、sendText/sendBinary 与三个 std::function 事件回调（onMessage/onConnected/onDisconnected），禁止拷贝。

## FILE: main/websocket/wsClient.cpp
功能：实现通用 WebSocket 客户端（薄封装 esp_websocket_client）：connect 构建 ws:// URI、disconnect/isConnected、sendText/sendBinary，事件回调在 WS Task 上下文触发 onMessage/onConnected/onDisconnected，供从机连接远程主机的 /ws/console 与游戏通道。

## FILE: main/websocket/wsServer.hpp
功能：声明 WebSocket 服务器 API：WsHandlerFn 处理器类型、生命周期（Start/Stop）、URI 注册/注销、发送文本/广播/关闭客户端，各游戏模块在 init/deinit 时自行注册注销。

## FILE: main/websocket/wsServer.cpp
功能：实现端口 8080 的 WebSocket 服务器：启动/停止（httpd，RealTime 优先级、max_uri_handlers=8）、wsServerRegisterWs/Unregister 动态注册 URI handler、wsServerSendText（异步帧，PSRAM 缓冲）与 wsServerBroadcastText 广播、wsServerCloseClient 关连接，供各游戏与 /ws/console、/ws/input 注册。

## FILE: main/host/hostManager.hpp
功能：声明 HostManager 单例：角色（isHost/isClient）、从机 connectToHost/getWsConnection、主机 4 槽位 ClientSlot、主机 mask 设置、事件回调 setOnSlotUpdate/setOnRoleChange，以及 wsConsoleHandler 调用的内部槽位接口。

## FILE: main/host/hostManager.cpp
功能：实现跨主机连接管理器单例：从机 connectToHost 建立 /ws/console 管理通道（注册、断开清理连接池、解析 registered/slots_update），连接池 createOrGetWsClient 同 URI 复用，主机侧维护 4 槽位 onClientRegistered/Disconnected、swapSlots 交换槽位+mask，并回调 UI 事件。

## FILE: main/host/inputHost.hpp
功能：声明 InputHost 单例：3 槽位 fd→hostId 映射，提供 registerClient/unregisterClient/getHostIdByFd/swapHostIds/hasFreeSlot 接口。

## FILE: main/host/inputHost.cpp
功能：实现远程客户端 fd→hostId(1-3) 映射表单例：registerClient 分配空闲槽位、unregisterClient 断开清理、getHostIdByFd 查询、swapHostIds 槽位交换，供 /ws/console 与 /ws/input 追踪来源。

## FILE: main/host/udpInput.hpp
功能：声明 UDP 手柄输入接口：udpInputStart 启动 8082 监听、udpInputCreateSocket/Send/CloseSocket 发送侧 API，并说明与 /ws/input 一致的 11 字节帧协议。

## FILE: main/host/udpInput.cpp
功能：实现 P4→P4 低延迟手柄输入 UDP 通道：udpInputCreateSocket/Send/CloseSocket 预 connect 持久 socket 发送 11 字节帧，udpInputStart 后台 Task（RealTime）监听端口 8082，recvfrom 解析帧注入 VirtualGamepad，并 60s 超时清理断连源。

## FILE: main/host/wsConsoleHandler.hpp
功能：声明 /ws/console WebSocket handler 及其配套接口 wsConsoleBroadcastSlotsUpdate/wsConsoleDisconnectSlot/wsConsoleHandleSlotSwap，并注释 JSON 注册/广播协议。

## FILE: main/host/wsConsoleHandler.cpp
功能：实现 /ws/console 设备管理通道：握手缓存 fd→IP，处理 register（分配 hostId、回 registered、更新 HostManager）、ping/pong、unregister、slot_swap，及断开时清理 InputHost/HostManager 并全量广播 slots_update；另提供广播、强制断开与槽位交换函数。

## FILE: main/host/wsInputHandler.hpp
功能：声明 /ws/input WebSocket handler 函数，注释 11 字节 GamepadState 二进制帧格式与经 InputHost 查询 hostId 再注入 VirtualGamepad 的流程。

## FILE: main/host/wsInputHandler.cpp
功能：实现 /ws/input WebSocket 入口（Web 客户端虚拟手柄用）：只接受 11 字节二进制帧，解析 hostId/player/GamepadState 后 trackFd 追踪并注入 VirtualGamepad，断开时对全部玩家 resetPlayer 清理。

## FILE: main/discovery/discovery.hpp
功能：声明 DiscoveredConsole 结构体（hostname/ip/port）与 discoverConsoles 发现函数（查询 _p4console._tcp、排除自身、同步阻塞）。

## FILE: main/discovery/discovery.cpp
功能：实现 mDNS 设备发现：discoverConsoles 同步查询 _p4console._tcp 服务（超时可配，默认 3s），解析 hostname/IPv4/port，按当前主机名过滤自身，供 ConsoleManager 枚举其他游戏主机。

## FILE: main/app/app.hpp
功能：App 基类头文件，声明运行状态（`running`/`deletable`）、生命周期钩子（`init`/`deinit`/`onForeground`/`onBackground`）、手柄输入回调（`onGamepadInput`）、AI 能力接口（`getAiContext`/`executeAiAction`/`getOpenableApps`/`onAiSelect`/`onAiActivate`）及 `AiSelectTarget`/`AiAppTarget` 结构体；所有派生 App 由 AppStack/AppStackManager 管理。

## FILE: main/app/app.cpp
功能：App 基类的实现：构造函数为每个 App 创建独立 LVGL screen（设置尺寸/滚动/默认字体），析构时等待 `deletable` 后持 LVGL 锁删除 screen；`pushApp`/`popApp`/`replaceWith`/`setManager` 均委托给 AppStackManager 完成栈导航。

## FILE: main/app/appStack.hpp
功能：AppStack 类头文件，声明 LIFO 单调用栈的 `push`/`pop`/`replace`/`top`/`depth` 接口，由 AppStackManager 友元创建和管理，内部持有 Display 指针与 App 向量；`pop` 在栈空时返回 orphan App 供调用方处理。

## FILE: main/app/appStack.cpp
功能：单栈实现：`push` 时让旧栈顶 `onBackground` 并 `init`+`applyApp`+`onForeground` 新 App，`pop`/`replace` 先切屏再异步删除旧 App；`scheduleDeletion` 用独立 Task 执行 deinit→等 deletable→持锁 delete，析构时按逆序清理栈内残留 App。

## FILE: main/app/appStackManager.hpp
功能：AppStackManager 类头文件，声明多栈管理接口（创建/销毁/切换栈、委托 push/pop/replace、`pushToNewStack` 用于从桌面开新游戏、`activeApp` 输入路由），内部持有多栈向量、活跃栈指针及 Mutex；栈切换时自动管理 App 生命周期钩子。

## FILE: main/app/appStackManager.cpp
功能：多栈导航管理器实现：管理多个 AppStack，支持 `createStack`/`destroyStack`/`switchToStack`/`pushToNewStack`，`pop` 时保护根栈（depth≤1 禁止弹出）并在非根栈变空时自动切回根栈+销毁空栈；`transitionStacks` 负责切换时触发 onBackground/onForeground 并加载新栈顶 screen。

## FILE: main/app/appFactory.hpp
功能：AppFactory 头文件，集中管理 name→App 的映射关系，声明静态工厂方法 `create(const std::string&, Display*)`，未识别的名称返回 nullptr；是 AI 系统与桌面打开应用的统一入口。

## FILE: main/app/appFactory.cpp
功能：应用工厂实现 `AppFactory::create(name, display)`，按应用名（兼容多种写法）映射创建 TetrisRoomApp/SnakeRoom/FruitNinjaRoom/ChineseChessRoom/TimeSettingsApp/WifiSettingsApp/BleSettingsApp/PowerManagementApp/ConsoleManagerApp/LuaManagerApp，供 DesktopApp 与 AiExecutor 共用。

## FILE: main/app/aiAgentApp/aiAgentOverlay.hpp
功能：AiAgentOverlay 单例类头文件，声明驻留 lv_layer_sys 的浮动窗（不参与 AppStack），提供 `start/show/hide/toggle` 及线程安全的 `reportLuaError/clearLuaError/reportFileRead/reportAiError`，并声明 IME 三态（拼/五/EN）、对话历史、长期记忆等私有状态与回调。

## FILE: main/app/aiAgentApp/aiAgentOverlay.cpp
功能：AI 助手浮动 Overlay 实现：在 `lv_layer_sys` 上构建可拖拽对话窗口，集成 LVGL 拼音/五笔输入法、手势/手柄组合键唤起、多轮 AI 对话（nextRound）、Lua 错误与文件读取回注 system prompt；定义默认 system prompt 模板（含 13 个系统操作与 Lua 生成协议）。

## FILE: main/app/aiAgentApp/pinyinDict.hpp
功能：PinyinDict 类头文件，声明 `loadFromFat`/`loadWubiFromFat`/`loadMixedFromFat` 三个单例缓存的词库加载接口（替换 LVGL 内置 101 条残缺词库），内部 `doLoad` 解析指定路径词库文件并注入 `lv_ime_pinyin_set_dict`。

## FILE: main/app/aiAgentApp/pinyinDict.cpp
功能：实现自定义词库加载：从 FAT 读取拼音/五笔/混合词库文本（每行`拼音<TAB>汉字串`），校验行合法性、按拼音字典序排序、在 PSRAM 紧凑分配 `lv_pinyin_dict_t` 数组并返回；加载失败或分配失败时回退 LVGL 内置词库。

## FILE: main/app/bleSettingsApp/bleSettingsApp.hpp
功能：BleSettingsApp 类头文件，继承 App，声明扫描/已连接区域的 UI 对象、5 个焦点组（标题/列表/槽位/卡片按钮/保存）、定时器与手柄导航方法，以及 AI 能力（`getAiContext`/`onAiSelect`/`onAiActivate`）和 `newApp` 静态工厂。

## FILE: main/app/bleSettingsApp/bleSettingsApp.cpp
功能：BLE 手柄设置 App 实现：提供扫描（5 秒冻结列表）、连接、断开、删除/移动配对、保存到 NVS 的界面；通过定时器轮询 BleGamepad 同步扫描结果与已连接槽位，实现手柄焦点导航、活动指示与 500ms 去重防误触。

## FILE: main/app/consoleManager/consoleManagerApp.hpp
功能：ConsoleManagerApp 类头文件，继承 App，声明 4 槽位 UI（nameLabel/maskBtns/swapBtn/disconnectBtn）、掩码状态 `m_maskState[4][4]`、交换模式、主机/从机视图容器、三级焦点导航（槽位卡片/掩码/操作）及 `applyMaskToVirtualGamepad` 等接口。

## FILE: main/app/consoleManager/consoleManagerApp.cpp
功能：多主机管理 App 实现：mDNS 扫描发现其他主机，BLE 配对风格主机/从机双视图；每槽 P0-P3 掩码按钮、交换/断开操作，通过 HostManager/wsConsoleHandler 同步槽位与 mask 到 VirtualGamepad；注册槽位更新与角色变化回调刷新 UI。

## FILE: main/app/desktopApp/desktopApp.hpp
功能：DesktopApp 类头文件，继承 App，声明 7 张卡片的数据（名称/简介/图标）、3 个焦点组（卡片/底部/状态栏）、顶栏各状态图标（时间/WiFi/蓝牙/音量/亮度/多主机/电池）以及导航、滑块、AI 能力（`getOpenableApps` 等）接口。

## FILE: main/app/desktopApp/desktopApp.cpp
功能：桌面启动器实现：构建 7 张游戏卡片（贪吃蛇/水果忍者/俄罗斯方块/斗地主/中国象棋/Lua 管理/游戏市场）与顶栏状态区，支持 3 焦点组手柄导航、时间/电池定时刷新、亮度/音量滑块，点击卡片经 `pushToNewStack` 打开对应 App。

## FILE: main/app/timeSettingsApp/timeSettingsApp.hpp
功能：TimeSettingsApp 类头文件，继承 App，声明时间显示/手动调整/同步控制等 UI 对象、SNTP 状态标志与 250ms 刷新定时器、3 个焦点组（标题/调整/同步）及 AI 能力接口和 `newApp` 工厂。

## FILE: main/app/timeSettingsApp/timeSettingsApp.cpp
功能：时间设置 App 实现：大字体显示当前时间，支持手动调整时/分/秒与秒归零、自动同步开关、SNTP 立即同步（超时 15s），定时刷新时间与 WiFi/同步状态标签，带手柄焦点导航与去重。

## FILE: main/app/wifiSettingsApp/wifiSettingsApp.hpp
功能：WifiSettingsApp 类头文件，继承 App，声明扫描列表/密码弹窗/主机名弹窗/连接信息卡的 UI 对象、3 个焦点组、定时器与导航方法，以及 `newApp` 静态工厂和 AI 接口。

## FILE: main/app/wifiSettingsApp/wifiSettingsApp.cpp
功能：WiFi 设置 App 实现：WiFi 开关、AP 扫描（10 秒合并去重）、密码键盘弹窗连接、断开、mDNS 主机名编辑弹窗，底部显示当前连接信息；触摸滚动结束时同步焦点索引，含主机名 NVS 持久化与 AI 能力。

## FILE: main/app/powerManagementApp/powerManagementApp.hpp
功能：PowerManagementApp 类头文件，继承 App，声明主机电量卡、4 手柄槽位、关机/重启按钮等 UI 对象、2 秒刷新定时器、3 个焦点组及 `doShutdown`/`doRestart`/AI 能力接口。

## FILE: main/app/powerManagementApp/powerManagementApp.cpp
功能：电源管理 App 实现：显示主机电量（图标/百分比/进度条/电压，含充电动画）与 4 个手柄电量槽位，提供关机（Deep-sleep）和重启按钮，定时刷新电量与活动指示，带焦点导航。

## FILE: main/app/testApp/testApp.hpp
功能：TestApp 类头文件，继承 App，声明构造（display, value）、背景色标签与提示文字、`nextAppChangeTime` 去重字段，用于验证 AppStack 多层 push/pop 生命周期。

## FILE: main/app/testApp/testApp.cpp
功能：测试用 App 实现：按传入 value 生成区分性背景色与大号 ID 标签，A 键 push 新 TestApp（随机值）、L3 键 pop，含 500ms 去重；deinit 模拟 3 秒延迟验证异步删除生命周期。

## FILE: main/app/luaGameApp/luaGameApp.hpp
功能：LuaGameApp 类头文件，继承 App，声明从文件/内存代码两种构造、`createFromAIGenerated` 静态工厂、Lua VM 与输入快照上下文、游戏循环 Task、看门狗 hook 以及 `requestExit`/`restart` 接口。

## FILE: main/app/luaGameApp/luaGameApp.cpp
功能：Lua 游戏运行 App 实现：初始化独立 Lua VM 并注册 display/input 绑定，创建 60fps 独立 Task 每帧持 LVGL 锁清空画布并调用 Lua `loop(dt)`；含 1000 万条指令/帧防死循环看门狗、错误上报 AiAgentOverlay、`createFromAIGenerated` 写入并运行 AI 生成脚本、restart/requestExit。

## FILE: main/app/luaManagerApp/luaManagerApp.hpp
功能：LuaManagerApp 类头文件，继承 App，声明 ScriptEntry 结构、浏览/重命名输入/确认删除三模式、列表与底部 4 按钮焦点组、删除确认与重命名 UI，以及 `scanScripts`/`rebuildList`/`runSelected` 等方法。

## FILE: main/app/luaManagerApp/luaManagerApp.cpp
功能：Lua 脚本管理器 App 实现：扫描 `/root/lua/game` 与 `/root/lua` 下所有 .lua 文件并排序展示，支持运行（创建 LuaGameApp）、删除（确认弹框）、重命名（键盘输入）、AI 生成入口（唤起 AiAgentOverlay），含焦点导航与列表重建。

## FILE: main/app/marketApp/marketApp.hpp
功能：MarketApp 类头文件，继承 App，声明市场脚本数据与下载状态、浏览/详情两模式、列表/底部/详情三焦点组、网格缩略图队列与详情视图对象，以及 `fetchList`/`downloadAndRun`/`sanitizeFileName` 等方法。

## FILE: main/app/marketApp/marketApp.cpp
功能：游戏市场 App 实现：通过 MarketClient 异步拉取脚本列表，3 列截图卡片网格（缩略图串行懒加载队列，未加载显示评分星占位），详情视图（大图+简介+元信息）、下载运行（存 `/root/lua/market/<id>.lua`，已装角标识别），网格 4 向导航与底部栏。

## FILE: main/app/chineseChess/chessLogic.hpp
功能：声明 `ChessLogic` 纯数据类（无 LVGL 依赖）：定义 Side/PieceType/Piece/Position/Move/State 等结构，提供 reset/selectPiece/movePiece/undoMove、合法走法查询、将军将死判定、Zobrist hash 接口及 9×10 棋盘坐标体系（红方 y=5~9 先走）。

## FILE: main/app/chineseChess/chessLogic.cpp
功能：中国象棋纯逻辑实现：Zobrist 哈希表初始化与增量更新、初始布局 `reset()`、选子/走棋/悔棋、7 种棋子的合法走法生成、将军/将死/飞将检测，以及带 hash 历史快照的走法记录（供 AI 搜索回溯恢复局面）。

## FILE: main/app/chineseChess/chessAI.hpp
功能：声明 `ChessAI` 类（AI 引擎 v2），组合 `TranspositionTable` 置换表与 `OpeningBook` 开局库，对外提供 `getMove(ChessLogic&)` 获取 AI 走法；内部定义 MAX_DEPTH=8、子力价值、活动性/将安全加成系数、杀手走法表及走法编码工具等私有接口。

## FILE: main/app/chineseChess/chessAI.cpp
功能：中国象棋 AI 引擎实现：包含各棋子 9×10 位置价值表、子力/位置价值评估函数 `evaluate()`、MVV-LVA 走法排序、静态搜索 `quiescence()` 以及迭代加深 Alpha-Beta 搜索（含杀手启发式、空着剪枝、置换表命中），在 600~1200ms 时间预算内为 `ChessLogic` 计算最佳走法。

## FILE: main/app/chineseChess/openingBook.hpp
功能：声明 `OpeningBook` 开局库：定义 BookMove（编码起止坐标+权重）与 OpeningLine（最多 12 个半走序列），提供 `getBookMoves()` 按历史查询开局走法，并给出走法紧凑编码规则（低位 from、高位 to，每格 x*10+y）。

## FILE: main/app/chineseChess/openingBook.cpp
功能：开局库数据实现：内置 8 条常见开局线（中炮对屏风马、飞相局、仙人指路等，红黑交替编码走法序列带权重），`getBookMoves()` 按当前走棋历史匹配开局线、汇总后续走法并按「权重+匹配深度加成」降序返回，供 AI 搜索优先采用。

## FILE: main/app/chineseChess/transpositionTable.hpp
功能：声明 `TranspositionTable` 置换表：以 Zobrist 64 位 hash 为键缓存已搜索局面，Entry 含 hash/score/depth/flag/bestFrom/bestTo（静态断言 16 字节），定义 EXACT/LOWER_BOUND/UPPER_BOUND 三种节点类型及 probe/store/clear 接口。

## FILE: main/app/chineseChess/transpositionTable.cpp
功能：置换表实现：2^18=262K 条目的哈希表（低 18 位索引），`probe()` 按 EXACT/LOWER_BOUND/UPPER_BOUND 标志校验深度并返回评估分与最佳走法，`store()` 采用「深度大者优先」替换策略，并提供 clear/usedCount 维护。

## FILE: main/app/chineseChess/chineseChessApp.hpp
功能：声明 `ChineseChessApp`（继承 App，支持 1P vs AI / 2P 本地对战），组合 `ChessLogic` 与 `ChessAI`，持有棋盘/棋子/光标/标记的 LVGL 对象池、视觉缓存、输入去抖与焦点导航等成员，并声明 createBoard/createPieces/createUI/syncBoard/gameLoop 等方法接口。

## FILE: main/app/chineseChess/chineseChessApp.cpp
功能：中国象棋游戏 App 实现：LVGL 预分配对象池渲染棋盘网格/九宫斜线/棋子/光标/走法标记，游戏线程 60fps 驱动 `ChessLogic` 并增量刷新画面；处理触摸点选与多手柄摇杆导航（P0 红方/P1 黑方独立光标）、AI 或双人对战回合推进及 GameOver 菜单。

## FILE: main/app/chineseChess/chineseChessRoom.hpp
功能：声明 `ChineseChessRoom`（继承 App 的游戏大厅，含 AI open_app 静态工厂 `newApp`），管理返回/人机/双人按钮、焦点索引与输入去抖，声明 createMenu/applyFocus/activateFocus 及 AI 能力接口 `getAiContext`/`onAiSelect`/`onAiActivate`。

## FILE: main/app/chineseChess/chineseChessRoom.cpp
功能：中国象棋大厅实现：创建「人机对战 / 双人对战」模式选择菜单，手柄摇杆+AB 键焦点导航，按钮回调经 `Task::addTask` 延后 push `ChineseChessApp`（opponentType 0/1），并实现 `getAiContext()`/`onAiSelect()`/`onAiActivate()` 供 AI 助手操作。

## FILE: main/app/fruitNinja/fruitNinjaLogic.hpp
功能：声明 `FruitNinjaLogic` 纯 C++ 逻辑类（无 LVGL 依赖）：定义 Direction/Button/FruitType/GameMode/State 枚举、Fruit/Particle 数据结构及 MAX_FRUITS/重力等常量，提供 reset/tick/handleGamepadInput/handleSwipe/handleTouch 核心操作与渲染数据读取接口。

## FILE: main/app/fruitNinja/fruitNinjaLogic.cpp
功能：水果忍者核心逻辑实现：水果抛物线物理、切片两半飞离动画、粒子系统、Classic(3命)/Arcade(60秒)两种计分规则；`handleGamepadInput()` 按「方向+按钮」匹配切水果，`handleSwipe()` 用线段-圆相交检测支持一刀切多个，炸弹爆炸扣命/扣分。

## FILE: main/app/fruitNinja/fruitNinjaRenderer.hpp
功能：声明 `FruitNinjaRenderer` 对象池渲染器：预分配 MAX_FRUITS 水果 lv_image（带 VFS 图片路径与颜色回退）、粒子、HUD 标签、三层白刃 lv_line 及 GameOver 界面，提供 create/render/showGameOver/addTrailPoint/ageTrail 等接口。

## FILE: main/app/fruitNinja/fruitNinjaRenderer.cpp
功能：水果忍者 LVGL 渲染器实现：预分配水果图片+提示标签/粒子/分隔线/HUD/GameOver/三层白刃轨迹线的对象池，`render()` 每帧同步逻辑状态到 LVGL 对象显隐与位置，维护环形缓冲轨迹点经 `ageTrail()` 老化淡出，并实现 showGameOver/updateHUD。

## FILE: main/app/fruitNinja/fruitNinjaApp.hpp
功能：声明 `FruitNinjaApp`（继承 App，支持 Classic/Arcade 模式与 1P/2P 左右分屏），组合 `FruitNinjaLogic` 与 `FruitNinjaRenderer`，含触摸跟踪、输入去抖、GameOver 按钮焦点及 gameLoop/触摸/按钮回调等成员接口。

## FILE: main/app/fruitNinja/fruitNinjaApp.cpp
功能：水果忍者游戏 App 实现：60fps 游戏线程驱动 `FruitNinjaLogic::tick()` 并调 `FruitNinjaRenderer::render()`；注册屏幕 PRESSED/PRESSING/RELEASED 触摸回调实现划动切水果（线段碰撞），手柄方向+按钮匹配输入，GameOver 后管理重新开始/返回按钮焦点。

## FILE: main/app/fruitNinja/fruitNinjaRoom.hpp
功能：声明 `FruitNinjaRoom`（继承 App 的游戏大厅，含 AI open_app 静态工厂 `newApp`），持有模式/人数按钮、选中状态与 BGM AudioHandle，声明 createMenu/updateBtnStyles/applyFocus/activateFocus 及 AI 能力接口。

## FILE: main/app/fruitNinja/fruitNinjaRoom.cpp
功能：水果忍者大厅实现：创建 Classic/Arcade 模式与 1P/2P 人数选择菜单并高亮选中态，手柄摇杆 6 项焦点导航 + A/B 键操作，开始按钮延后 push `FruitNinjaApp`，进入大厅循环播放 BGM，并提供 AI 上下文/选择/激活接口。

## FILE: main/app/snake/snakeGame/gameLogic.hpp
功能：声明 `SnakeGameLogic` 纯数据逻辑类（无 LVGL 依赖）：定义 Cell 枚举(1~4 蛇身/食物)、Direction/State、Snake（deque 身体+待处理方向）与 Rect 禁止区域，提供 reset/tick/setDirection/getCell/addForbiddenZone 等接口及 DirUtils 方向工具命名空间。

## FILE: main/app/snake/snakeGame/gameLogic.cpp
功能：贪吃蛇核心逻辑实现：支持 1~4 名玩家在四角独立出生并各带专属起始方向，`tick()` 分三阶段推进（移动+碰撞判定→清死亡蛇身→胜负判定），支持撞墙/撞自身/撞他蛇、食物生成（避开禁止区域）、禁止 180° 掉头与 DirUtils 方向工具函数。

## FILE: main/app/snake/snakeGame/snakeGame.hpp
功能：声明 `SnakeGame`（继承 App，支持 1~4 人同屏、触摸 D-pad/手柄/未来 WS 远程输入），组合 `SnakeGameLogic`，声明对象池与 D-pad 创建、updateScene、gameLoop 及各玩家方向按钮回调、焦点导航等接口。

## FILE: main/app/snake/snakeGame/snakeGame.cpp
功能：贪吃蛇游戏 App 实现：按屏幕分辨率动态计算网格并创建 LVGL 蛇身/食物/网格线对象池、蛇头呼吸动画与四角触摸 D-pad，D-pad 覆盖区注册为食物禁止生成区域；游戏线程驱动 `SnakeGameLogic::tick()` 并增量更新场景，处理 GameOver 重新开始/返回。

## FILE: main/app/snake/snakeRoom/snakeRoom.hpp
功能：声明 `SnakeRoom`（继承 App 的贪吃蛇大厅，含 AI open_app 静态工厂 `newApp`），管理返回与 1~4P 人数按钮、焦点导航去抖和 BGM AudioHandle，声明 createMenu/applyFocus/activateFocus 及 AI 能力接口。

## FILE: main/app/snake/snakeRoom/snakeRoom.cpp
功能：贪吃蛇大厅实现：创建 1P/2P/3P/4P 人数选择菜单，手柄摇杆+AB 焦点导航，按钮回调带防重并延后 push `SnakeGame`，进入大厅播放 BGM，提供 AI 上下文/选择/激活接口供 AI 助手选人数开局。

## FILE: main/app/tetris/gameLogic/gameState.hpp
功能：定义跨三层（GameLogic→Renderer→Network）传递的扁平可序列化纯数据 `GameState` 结构体（棋盘、当前方块、Ghost、预览队列、Hold、计分、对战垃圾行/攻击目标、gameOver/active 及粘性 `boardChanged` 渲染同步标志），由 `PlayerState::exportState()` 写入、渲染器与网络层读取。

## FILE: main/app/tetris/gameLogic/tetris_client.hpp
功能：声明 Tetris 纯逻辑层的数据结构与常量：棋盘 10×22 坐标系、`Piece`/`Board`/`PieceQueue`/`Scoring` 类、SRS 形状与踢墙表外部数据、攻击行计算、Ghost 计算、T-Spin 检测及 `FastRng` 轻量随机数等，为渲染器和网络层提供统一游戏模型。

## FILE: main/app/tetris/gameLogic/tetris_client.cpp
功能：实现纯游戏逻辑基础组件：SRS 七种方块 4×4 形状与 I/JLSTZ 踢墙表、`Piece` 方块（取格子/旋转/平移）、`Board` 棋盘（碰撞/放置/消行/垃圾行注入/列掩码/dirty 行追踪）、`PieceQueue` 7-bag 随机出块动态链表、`Scoring` 计分系统及 T-Spin 三角几何判定，全程不依赖 LVGL。

## FILE: main/app/tetris/gameLogic/player_state.hpp
功能：声明 `PlayerState` 类及 `DirtyFlags` 脏标志（方块/幽灵/棋盘/Hold/分数），包含棋盘、计分、当前方块、重力/Lock/DAS/ARR 计时器、键盘与网络输入状态、共享 PieceQueue 游标、音效回调及全部游戏逻辑接口；Host-authoritative 模式下只运行在主机端。

## FILE: main/app/tetris/gameLogic/player_state.cpp
功能：实现单玩家核心游戏逻辑 `PlayerState`：方块生成/移动/带 Wall Kick 旋转/硬降、锁定消行（含 T-Spin 三角判定、B2B/Combo 计分、垃圾行抵消与攻击输出）、Hold 交换、DAS/ARR 输入处理、重力+Lock Delay 帧更新，并通过 `exportState()` 导出到 `GameState` 供渲染与联机使用。

## FILE: main/app/tetris/net/tetris_net.hpp
功能：声明 WebSocket 联机层的消息标签（join/join_ack/input/snapshot）、Host/Client 回调接口（`TetrisHostCallbacks`/`TetrisClientCallbacks`）以及 `tetrisNetInit/Update/Deinit`、Host 发 snapshot、Client 连接/发输入等 API，并导出 `gameStateFromJson` 供 TCP 客户端复用。

## FILE: main/app/tetris/net/tetris_net.cpp
功能：实现 Tetris WebSocket 网络层：`GameState` 与 JSON 的双向序列化、注册 `/ws/tetris` handler（Host 处理 join/input、回复 join_ack）、Host 发送/广播 snapshot、Client 用 `esp_websocket_client` 连接并处理消息队列，回调驱动 `onJoin/onInput/onSnapshot`。

## FILE: main/app/tetris/net/tetrisTcpHost.hpp
功能：声明 P4-to-P4 专用 TCP 主机层接口：`TetrisTcpHostCallbacks` 回调、`tetrisTcpHostStart/Stop`（默认 8083）、设置玩家数、检测客户端、非阻塞 `PushSnapshots` 快照推送及主动连从机函数，协议与 `/ws/tetris` 一致但走裸 TCP 换行分隔 JSON。

## FILE: main/app/tetris/net/tetrisTcpHost.cpp
功能：实现 P4→P4 的 Tetris TCP 游戏服务器（端口 8083）：accept Task 接纳客户端、每连接 recv Task 解析 join/input（回复 join_ack），独立 Send Task 以 20fps 异步序列化并推送所有玩家 snapshot（不阻塞游戏循环），并支持 `tetrisTcpHostConnectToClient` 反客为主主动连从机 8084。

## FILE: main/app/tetris/net/tetrisRemoteClient.hpp
功能：声明 `TetrisRemoteClient` 类：TCP 连接生命周期（listen/disconnect）、线程安全读取各玩家 `GameState`（getPlayerState/hasNewState/consumeNewState）、发送按键与 join、断连回调，及内部行缓冲与 recv Task 结构。

## FILE: main/app/tetris/net/tetrisRemoteClient.cpp
功能：实现从机端远程客户端 `TetrisRemoteClient`：在 8084 端口 listen 等待主机主动建立 TCP 连接，后台 recv Task 按换行分割 JSON 解析 join_ack 与多玩家 snapshot（互斥锁保护、逐玩家新状态标志），并提供 sendInput/sendJoin 发送输入到主机。

## FILE: main/app/tetris/renderer/tetris_renderer.hpp
功能：声明 `TetrisRenderer` 类及 Guideline 标准色板：绑定 `GameState*` 数据源、`PlayerState*` 与 `HostContinuousInput` 输入缓冲，提供 syncState 增量渲染入口、退出/切换攻击目标回调，以及棋盘格、预览格、触屏按钮等 LVGL 对象成员与内部渲染辅助方法。

## FILE: main/app/tetris/renderer/tetris_renderer.cpp
功能：实现 LVGL 渲染器 `TetrisRenderer`：按玩家宽度自适应格子尺寸布局棋盘/预览队列/攻击/连击/垃圾信息与触屏按钮，`syncState()` 基于视觉缓存与 dirty row mask 做 diff 增量渲染（方块/Ghost 移动增量清除重画），并将触屏按钮写入 PlayerState 输入。

## FILE: main/app/tetris/tetrisApp.hpp
功能：声明 `TetrisApp` 主游戏类：Host-authoritative 架构，包含共享 PieceQueue、`PlayerState` 数组、导出的 `GameState` 数组、每玩家 `TetrisRenderer`、游戏线程与渲染 Task，多 host 输入合并缓冲、Game Over UI、手柄按钮映射及重新开始/返回房间去重逻辑。

## FILE: main/app/tetris/tetrisApp.cpp
功能：实现主机权威的俄罗斯方块主游戏 App：按玩家数等宽分屏创建渲染器，初始化共享 PieceQueue 与多个 `PlayerState`，启动 WS 网络层与 TCP 8083 服务器并主动连接从机，`gameLoop` 60fps 内合并多主机连续输入→推进逻辑→攻击路由→导出 GameState→20fps 发 snapshot，另有独立 30fps 渲染 Task 与全局 Game Over 界面。

## FILE: main/app/tetris/tetrisRoomApp.hpp
功能：声明 `TetrisRoomApp` 房间 App 类：玩家数选择、BLE 手柄配对入口（跳转 BleSettingsApp）、本地/远程游戏启动接口、槽位卡片与状态刷新、焦点组导航、动作去重、BGM 播放及 `getAiContext`/`onAiSelect`/`onAiActivate` AI 能力。

## FILE: main/app/tetris/tetrisRoomApp.cpp
功能：实现俄罗斯方块房间/选人界面：从机模式下自动跳转远程游戏，构建返回/1P/2P/3P/手柄配对/开始游戏与玩家槽位卡片 UI，三焦点组导航与手柄输入处理，展示手柄连接状态与活动指示，并通过 `startGame`/`startRemoteGame` 分别启动本地 `TetrisApp` 或远程 `TetrisRemoteApp`，同时接入 AI 焦点探索接口。

## FILE: main/app/tetris/tetrisRemoteApp.hpp
功能：声明 `TetrisRemoteApp` 从机渲染 App：持有 `TetrisRemoteClient`、最多 3 个 `TetrisRenderer` 与持久 `GameState` 数组，运行 30fps 渲染 Task、连接状态标签 UI 及输入去重，用于全屏渲染远程主机的多玩家分屏画面。

## FILE: main/app/tetris/tetrisRemoteApp.cpp
功能：实现从机端远程游戏 App：创建 3 个等宽渲染器并绑定 `TetrisRemoteClient`，通过 Task 在 8084 监听等待主机 TCP 连接后发送 join，30fps 渲染 Task 动态按玩家数显隐分屏、同步并消费新 snapshot；手柄输入由系统层 UDP 自动转发，断连时才允许 B 返回。

---
