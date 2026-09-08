# 模块索引

## 源码目录结构

```
main/
├── main.cpp                    ← 程序入口 app_main()
├── CMakeLists.txt              ← 主 CMake 配置
├── idf_component.yml           ← 组件依赖
│
├── aiAgent/                    ← AI Agent 核心
│   ├── aiProtocol.hpp          ← 动作/响应协议定义
│   ├── aiConfig.hpp            ← API Key 配置加载
│   ├── aiClient.hpp            ← HTTPS POST 调用
│   ├── aiParser.hpp            ← JSON 响应解析
│   ├── aiExecutor.hpp          ← 动作顺序执行（14 个系统操作）
│   └── aiSystemInfo.hpp        ← 系统信息采集
│
├── app/                        ← 应用层
│   ├── app.hpp / app.cpp       ← App 基类
│   ├── appStack.hpp / .cpp     ← 单栈导航
│   ├── appStackManager.hpp/.cpp← 多栈管理器（Meyer's Singleton）
│   │
│   ├── desktopApp/             ← 桌面启动器（动态 ELF 卡片）
│   ├── aiAgentApp/             ← AI 助手浮动窗 + 输入法
│   ├── luaGameApp/             ← Lua 游戏运行器（60fps 循环）
│   ├── luaManagerApp/          ← Lua 脚本管理器
│   ├── marketApp/              ← 游戏市场 UI
│   ├── consoleManager/         ← 多主机管理 UI
│   ├── bleSettingsApp/         ← BLE 手柄配对
│   ├── wifiSettingsApp/        ← WiFi 配置
│   ├── timeSettingsApp/        ← 时间设置
│   ├── powerManagementApp/     ← 电源管理
│   └── testApp/                ← 测试用
│
├── audio/                      ← 音频子系统
│   ├── ES8311.hpp / .cpp       ← ES8311 编解码器驱动
│   └── Audio.hpp / .cpp        ← 音频管理器（Mixer + AudioHandle）
│
├── battery/                    ← 电池管理
│   ├── batteryManager.hpp/.cpp ← ADC 电量检测 + 充电检测
│   └── batteryMonitor.hpp/.cpp ← 低电量弹窗提醒
│
├── bleGamepad/                 ← BLE 手柄
│   └── bleGamepad.hpp / .cpp   ← NimBLE + esp_hosted
│
├── discovery/                  ← mDNS 设备发现
│   └── discovery.hpp / .cpp
│
├── display/                    ← 显示子系统
│   ├── display.hpp / .cpp      ← LVGL 适配层 + LockGuard
│   ├── ili9881c.cpp            ← MIPI DSI 面板驱动
│   └── font.cpp                ← FreeType 字体加载
│
├── elfManager/                 ← ELF 插件管理
│   └── elfManager.hpp / .cpp   ← dlopen/dlclose + refcount
│
├── elfSdk/                     ← ELF SDK 契约
│   ├── elf_sdk.h               ← ABI 定义（v4）
│   ├── elf_symtab.c            ← 符号表（脚本生成）
│   ├── elf_sdk.cpp             ← 符号解析哈希 + 启动优化
│   └── dsoHandle.c             ← .so 链接支持
│
├── gamepad/                    ← 手柄抽象
│   ├── gamepadState.hpp        ← GamepadState 结构体
│   └── virtualGamepad.hpp      ← 统一输入抽象层（60fps Dispatch）
│
├── gpio/                       ← GPIO 封装
│   └── gpio.hpp / .cpp
│
├── gui/                        ← GUI 辅助
│   └── gui.hpp / .cpp          ← 色板 + 16 个工厂方法
│
├── host/                       ← 跨主机联机
│   ├── hostManager.hpp / .cpp  ← 角色管理 + 槽位 + 连接池
│   ├── wsConsoleHandler.cpp    ← /ws/console 管理通道
│   ├── wsInputHandler.cpp      ← /ws/input 手柄注入
│   ├── udpInput.cpp            ← UDP P4→P4 输入转发
│   └── inputHost.hpp           ← fd→hostId 映射
│
├── iic/                        ← I²C 总线
│   └── iic.hpp / .cpp
│
├── logService/                 ← 日志服务
│   └── logService.hpp / .cpp   ← vprintf 重定向 + /ws/log
│
├── lua/                        ← Lua 引擎
│   ├── luaRuntime.hpp / .cpp   ← VM RAII 封装
│   └── bindings/               ← C 绑定模块
│       ├── luaDisplay.*        ← 绘图 API（LVGL 对象池化）
│       ├── luaInput.*          ← 手柄输入（帧快照）
│       ├── luaAudio.cpp        ← 音效播放
│       ├── luaTouch.*          ← 多点触控
│       ├── luaStorage.*        ← 安全存档
│       └── luaThread.cpp       ← 延时/随机
│
├── market/                     ← 市场客户端
│   ├── marketClient.hpp        ← HTTP 异步封装
│   └── marketConfig.hpp        ← 服务器地址配置
│
├── monitor/                    ← 系统监视器
│   └── systemMonitor.hpp/.cpp  ← CPU + 内存 + FPS
│
├── mutex/                      ← 互斥锁
│   └── mutex.hpp / .cpp        ← RAII Mutex + Lock
│
├── ota/                        ← OTA 升级
│   └── otaManager.hpp / .cpp   ← 双槽 OTA + 回滚 + 内部 RAM 任务
│
├── power/                      ← 电源管理
│   └── power.hpp / .cpp
│
├── screenStream/               ← 屏幕串流
│   ├── screenStream.hpp/.cpp   ← MJPEG 硬件编码 + 帧回调
│   └── wsStreamHandler.cpp     ← /ws/stream 双任务管线
│
├── server/                     ← HTTP 服务器
│   ├── serverKernal.hpp/.cpp   ← 裸 socket HTTP + 路由
│   └── buildinHtml/            ← 内置 HTML（file.html 等）
│
├── storage/                    ← 存储子系统
│   ├── fat.hpp / .cpp          ← FAT 分区 → /root/
│   ├── mem.hpp / .cpp          ← 内存文件系统 → /root/mem
│   └── sd.hpp / .cpp           ← SD 卡 → /root/sd
│
├── task/                       ← 协程调度器
│   └── task.hpp / .cpp         ← 轮询式 Task + Thread 封装
│
├── time/                       ← 时间服务
│   └── timeService.hpp / .cpp  ← SNTP + NVS 持久化
│
├── touch/                      ← 触摸子系统
│   ├── touch.hpp / .cpp        ← GT911 驱动
│   └── gt911Indev.hpp / .cpp   ← 五 indev 多指桥接
│
├── utility/                    ← 工具类
│
├── virtualIndev/               ← 虚拟触摸输入
│   ├── virtualIndev.hpp/.cpp   ← WebSocket 触摸注入
│   └── wsTouchHandler.cpp      ← /ws/touch handler
│
├── websocket/                  ← WebSocket
│   ├── wsServer.hpp / .cpp     ← 服务器端
│   └── wsClient.hpp / .cpp     ← 客户端封装
│
└── wifi/                       ← 网络
    ├── wifi.hpp / .cpp         ← WiFi STA/AP + NAT
    ├── slave.hpp / .cpp        ← C6 协处理器管理
    ├── mdns.hpp / .cpp         ← mDNS 服务
    ├── nvs.hpp                 ← NVS 初始化
    ├── http.hpp                ← HTTP 协议解析
    ├── socketStream.hpp        ← TCP 流封装
    └── socketStreamWindow.hpp  ← 连接池管理

elfapps/                        ← ELF 插件游戏
├── snake/                      ← 贪吃蛇
├── chineseChess/               ← 中国象棋
├── fruitNinja/                 ← 水果忍者
├── tetris/                     ← 俄罗斯方块
└── douDiZhu/                   ← 斗地主

tools/                          ← 构建工具
├── build_elf.sh                ← ELF 插件构建脚本
├── gen_elf_syms.py             ← 符号表生成工具
├── gen_pinyin_dict.py          ← 拼音词库生成
├── gen_wubi_dict.py            ← 五笔词库生成
└── gen_wubi_pinyin_dict.py     ← 混合词库生成

patches/                        ← LVGL/ESP 组件补丁
├── 0001-esp_lvgl_adapter-frame-ready-callback.patch
├── 0002-lvgl__lvgl-ime-pinyin-fixes.patch
├── 0003-esp_lv_decoder-disable-png.patch
└── apply.py                    ← 跨平台补丁管理

resourse/                       ← FAT 资源文件
├── lua/                        ← Lua 测试脚本
├── server/                     ← Web 静态资源
│   ├── index.html              ← 主页
│   ├── screen/                 ← 屏幕串流查看器
│   ├── tetris/                 ← Tetris Web 客户端
│   ├── gamepad/                ← 虚拟手柄
│   ├── wifi/                   ← WiFi 配置页
│   ├── ota/                    ← OTA 升级页
│   └── diag/                   ← 系统诊断页
└── system/                     ← 系统文件
    ├── NotoSC.ttf              ← 全局字体
    ├── pinyin_dict.txt         ← 拼音词库
    ├── wubi_dict.txt           ← 五笔词库
    └── wubi_pinyin_dict.txt    ← 混合词库
```

## 模块依赖关系

```
main.cpp
  ├── Display (LVGL 适配)
  │   ├── ILI9881c (面板驱动)
  │   ├── FontLoader (字体)
  │   └── GT911Indev (触摸)
  ├── AppStackManager (导航)
  │   └── App (基类)
  │       ├── DesktopApp
  │       ├── Settings Apps
  │       ├── LuaManagerApp
  │       └── MarketApp
  ├── ElfManager (插件)
  │   └── elfSdk (ABI)
  ├── VirtualGamepad (输入)
  │   ├── BleGamepad (BLE)
  │   ├── udpInput (P4→P4)
  │   └── wsInputHandler (Web)
  ├── Audio (音频)
  │   └── ES8311 (编解码器)
  ├── BatteryManager (电池)
  ├── BatteryMonitor (低电量)
  ├── AiAgentOverlay (AI)
  │   └── aiAgent/ (核心)
  ├── LogService (日志)
  ├── ScreenStream (串流)
  ├── OtaManager (升级)
  ├── HostManager (联机)
  ├── TimeService (时间)
  └── SystemMonitor (监控)
```
