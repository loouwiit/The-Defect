# elfPlan.md — ELF 游戏插件化（开发中活文档）

> **本文件定位**：ELF 游戏插件化的**活的开发文档**（本分支专属，内部不分发）。所有设计决策、当前状态、ABI 结论、测试计划/结果、路线图都在这里高频更新。
>
> **⚠ 分支策略**：ELF 改造是大工程，**全部完成前不合并主分支、不分发**。`AGENTS.md` / `readme.md` 待整个 ELF 大工程完成后**一次性**修订回填（避免迁移期间频繁改动污染 AI 上下文），此前的所有改动/决策以本文件为准。
>
> **相关代码**：`main/elfSdk/`（ABI + 符号表）、`main/elfManager/`（模块管理）、`elfapps/`（.so 插件工程）、`tools/`（构建/校验脚本）、`main/app/testApp/`（测试台）。

---

## 1. 目标

系统固件（desktop + 基础库 + 系统 App）常驻，游戏编译为 **ELF 插件 (.so)** 放 FAT 分区，运行时 `dlopen` 动态加载 → **单 App 增量更新**（改一个游戏只重编译该游戏，HTTP PUT 上传即可，**无需重烧主固件**）。

- 主固件瘦身（当前 ~5MB → 目标 ~2.5-3MB）
- 游戏更新热替换（改→编→传→重进见新）
- 保留原生 App 的全部灵活性（自建线程/事件循环/网络）

## 2. 架构（App\* 工厂模型）

**关键设计决策（用户拍板，替代早期"容器"方案）**：不建容器，插件本身继承主固件 `App`。

```
主固件（C++ monolith）                        .so 插件工程（elfapps/<name>/，C++）
├─ elfSdk 层                                  ├─ 唯一 C 入口 elf_plugin_entry()
│   ├─ elf_symtab.c    — 符号表（脚本生成）   │   └─ 返回 elf_plugin_info_t
│   ├─ elf_sdk.cpp     — 注册符号表 + 启动    │       { version, create_app, destroy_app }
│   │                   优化（日志静默 +      │         （标识/元数据统一走插件包
│   │                   符号哈希 resolver）    │         manifest.json）
│   └─ gen_elf_syms.py — 白名单生成工具       ├─ XxxElfApp : public App（.so 内 new）
│                                            ├─ 自建事件循环/线程（原生灵活性）
├─ ElfManager                                  └─ 调用主固件符号（lockGuard/Task/单例…）
│   ├─ 内部 RAM 栈任务 dlopen + version 校验
│   ├─ refcount + size/mtime 热更新检测
│   └─ AppStack delete 后 → dlclose
└─ AppStack（对 ELF App 与原生 App 一视同仁）
```

- 主固件 `push(new App{params})` 换成 `push(ElfManager::createApp(path, display, params))`
- **AppStack 全权管理生命周期**：push/pop/init/deinit/onForeground/onBackground/onGamepadInput 全部虚分派进 .so（vtable 在 PSRAM）
- 插件与原生 App 完全一致：自建事件循环/线程、onGamepadInput 直收输入、display->lockGuard() 直接调用

## 3. 当前状态（2026-08-09：R1 + Phase 1~4 + **启动优化 + 插件包 + ABI v4 全部完成，设备全链路验证通过**）

**启动优化 + 插件包 + ABI v4（2026-08-09，✅ 已验证）**：

- **日志静默**：`elf_sdk_init()` 里 `esp_log_level_set("ELF"/"DLMOD", WARN)` —— esp_elf_loader 组件逐符号 INFO（启动 ~850 行）全部消失，错误级保留，零组件 patch
- **符号解析哈希化**：`elf_sdk_init()` 构建 FNV-1a 2048 桶哈希索引（PSRAM ~26KB，name/sym 指针零拷贝直指符号表内）+ `elf_set_symbol_resolver(elfSdkResolve)` —— 每次 dlopen 重定位的符号查找从线性 strcmp（~1000+ 次）→ O(1)，未命中兜底 `elf_find_sym_default`（extern C 声明，不 include private 头）
- **插件包格式（替代平铺 .so）**：`/root/elf/<name>/{manifest.json, <name>.so, icon.jpg}` —— 元数据全部外置 manifest（name/display_name/description/main/icon，build_elf.sh 由 `plugin_meta.json` 生成），桌面扫描**零 dlopen**（`listElfApps` + `readManifest`，FAT + cJSON）→ 5 个图标秒级全显示
- **ABI v4**：`elf_plugin_info_t` 只剩 `version`/`create_app`/`destroy_app`（v3 去 display_name/description/icon_path，v4 再删 name）—— 标识与元数据统一 manifest 单一来源，ABI 不再因元数据变更升级
- **实测**：启动无 ELF/DLMOD INFO 日志；游戏 dlopen 加载 46ms（哈希化前 ~600ms+）；桌面卡片即时全显；退出 refcount=0 → dlclose 干净

**Phase 3 联机游戏全部迁移完成（tetris / douDiZhu，设备全链路验证通过）**：

- 主固件瘦身：5.06MB → **4.79MB**（桌面只剩系统卡片，游戏全走 `/root/elf/*.so` 动态扫描）
- 符号表 2055 → **2133 符号**（网络符号面一次性解决：lwip 51 / httpd 46 / esp_websocket 9 / xQueue 20 / wsServer 5）
- **网络符号导出（Phase 3 核心）**：`gen_elf_syms.py` 白名单加 `lwip_`/`httpd_`/`esp_websocket_`/`xQueue` 前缀（lwip socket 族全是宏映射：`socket()`→`lwip_socket`/`close()`→`lwip_close`，`LWIP_COMPAT_SOCKETS` 确认）+ **`CPP_FN_PREFIXES`**（wsServer 5 个函数是 C++ mangled、无类名，demangle 后按前缀收）+ **`DATA_EXACT`**（`__dso_handle` 是 D 类型数据符号，脚本默认只收函数类型）
- **插件 .so 自足 `__dso_handle`**：每个插件工程加 `dsoHandle.c`（`void* __dso_handle = (void*)&__dso_handle;`）——`.so` 用 `-nostdlib` 链接（不链接 crtbeginS.o），GNU ld 对 shared 目标 `__dso_handle` 引用做**硬检查**（undefined reference，非可留空 UND）；带析构的函数局部 static（含 `std::function`）会触发
- **tetris 联机验证 ✅**：WS 玩家加入 `/ws/tetris`、TCP 8083 服务器 + send task 20fps、HostManager 槽位全正常；**douDiZhu ✅**：单机 AI 模式正常（联机待测，需另一块板子）
- **回归 ✅**：snake / chineseChess / fruitNinja 旧插件符号表重构后仍正常；AI `open_app()` 动态路由正常

**Phase 2 单机游戏全部迁移完成（snake / chineseChess / fruitNinja，设备全链路验证通过）**：

- 桌面动态卡片自动出现（ABI v4 起标识与元数据改走插件包 manifest.json，`elf_plugin_info_t` 只保留运行契约 version/create_app/destroy_app）；退出 `refcount=0` → `dlclose` 干净卸载
- **符号保活机制（本项目核心经验）**：插件符号缺失分两类——
  1. **白名单漏选**（.elf 有但脚本过滤掉，如 `sqrt`/`fseek`/`__clzsi2`）→ `gen_elf_syms.py` 补 `C_EXACT`/`LIBGCC_PREFIXES`（libgcc 辅助族含 `__clz/__ctz/__popcount`）
  2. **主固件没引用**（.elf 没有）→ `main/CMakeLists.txt` `target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=<sym>")`——**必须 INTERFACE**（main 是静态库，PRIVATE 静默失效）；`used,retain` 数组在 ESP-IDF 链接脚本下**无效**（节被 GC）
- **构建时符号检查**：`tools/check_elf_syms.sh`（build_elf.sh 自动调用）`nm -u <so>` 对照 `.elf`，**含弱符号**，缺失即拦截上传
- **鸡生蛋重建**：`gen_elf_syms.py --empty > elf_symtab.c`（空表占位）→ build → 生成真实表 → build；脚本出错也输出空表+非零退出（`>` 重定向后文件永远合法）

**Phase 4 桌面动态扫描（✅ 已验证）**：

- **ABI v2**（`ELF_SDK_VERSION` 1→2）：`elf_plugin_info_t` 扩展 `display_name`/`description`/`icon_path`（可 NULL），插件自带中文名/描述/图标 → 桌面卡片无需主固件硬编码
- **ElfManager 多模块化**：`Module m_module` → `std::map<std::string, Module>`（path 键）；`App::m_elfModule` void* → `char m_elfModulePath[64]`（map 节点地址漂移，改存 path）；锁粒度双层（map 轻锁 + 慢操作锁外）
- **桌面动态卡片**：`GAME_COUNT` 静态数组 → 动态 `m_cards`（ELF 插件卡片在前 + 内置 4 游戏 + 系统 3 卡片）；`startElfScan()` 后台渐进式：`listElfFiles`（快，占位卡）→ 逐个 `inspectModule`（dlopen→读元数据→dlclose，不增 refcount）→ 按 path 定位更新卡片
- **AI 动态能力**：`AiAppTarget` 加 `elfPath` 字段（aiExecutor 优先走 `ElfManager::createApp`）；`getOpenableApps()` 动态并入 ELF 插件；`getAiContext()` cards 描述动态生成

**Phase 2 实测（snake 插件，设备验证）**：

```
ElfManager: dlopen OK: name=snake version=1 create_app=0x...   （~620ms / 682KB）
ElfManager: createApp OK: app=0x... refcount=1                 （SnakeRoom 从插件创建）
SnakeGame: 游戏循环启动                                        （插件内 Thread + gameLoop）
SnakeRoom: 多次点击，已过滤                                     （输入/防抖/焦点全走 .so）
AppStack: push/pop                                             （原生栈全权管理插件 App）
ElfManager: App 销毁，refcount=0                               （退出 → dlclose，无泄漏）
AI open_app("snake") → getAiContext("双人模式") → 焦点@:1       （AI 上下文/动作进 .so）
```
- 游戏表现与原生**无差异**；`refcount=0 → dlclose` 回收正常；无 .so 时入口优雅提示不崩
- `SnakeRoom::newApp` 旧 AI 工厂已清理（AI 启动走主固件 `createSnakePlugin` 适配器 → ElfManager）

**R1 实测证据（串口日志）**：

```
ElfManager: dlopen OK: name=smoke version=1 create_app=0x...   （30ms / 141KB）
ElfManager: createApp OK: app=0x48c13e28 refcount=1            （App* 在 PSRAM 堆）
[ELF-TEST] init: stdlib=PASS fnLocal=PASS fileStatic=0x0
  ├─ stdlib=PASS       → C++ 运行时符号解析成功（std::vector/string）
  ├─ fnLocal=PASS      → 函数局部 static 构造成功（__cxa_guard 生效）
  └─ fileStatic=0x0    → 文件级非 POD static 未构造（.init_array 不跑，实测确认）
[ELF-TEST] onForeground / onBackground / deinit                 （虚分派进 .so）
AppStack: push / pop                                             （原生生命周期全接管）
ElfManager: App 销毁，refcount=0 → dlclose → Removed node        （干净卸载）
```

**R1 验证的 7 项技术假设 → 全部打掉**：虚函数跨边界+App 基类布局 / std::vector/string / new-delete 落 PSRAM / -fno-exceptions 无隐藏引用 / vtable/GOT 在 PSRAM / 卸载后内存回收 / **loader 不执行 .init_array**。

## 4. 核心机制

### 生命周期

```
push(ElfManager::createApp(...))
  └─ refcount++ → so->create_app() → new XxxElfApp（继承 App，虚分派进 .so）
AppStack: init/onForeground/onGamepadInput/onBackground … 全部走 .so vtable
pop → scheduleDeletion 任务: deinit() → delete app（虚析构在 .so 内跑）
  └─ delete 后 → ElfManager::onAppDestroyed(app->m_elfModule) → refcount-- → 0 时 dlclose
```

- **清理时机**：`delete app` **之后**（不能在 deinit 时 dlclose——析构还在跑 .so 代码会崩）
- **dlclose 无 arch_flush**（`esp_elf_deinit` 只 free 内存）→ 普通 PSRAM 栈可跑 → 直接在 `scheduleDeletion` 任务里执行
- **dlopen 须内部 RAM 栈**（`esp_elf_arch_flush` 调 `spi_flash_disable_interrupts_caches_and_other_cpu`，assert 栈在内部 RAM）→ ElfManager 用 `xTaskCreateStatic` + 内部 RAM 静态栈
- App 基类新增 `setElfModule(void*)` / `m_elfModule`；AppStack 两处删除点（`scheduleDeletion` 任务内 + `~AppStack` 循环）`delete` 前取句柄、后回调

### 版本守卫

插件/主固件同编译 `elf_sdk.h` 的 `ELF_SDK_VERSION`，不匹配拒绝加载。
**救场案例**：曾因"把 dlsym 函数指针当结构体解引用"读到假版本 1303（函数机器码）——版本校验拦下了必然的崩溃。

## 5. 符号导出（单表脚本生成 + 白名单，`elf_sdk_init()` 在 main.cpp 启动早期注册）

**单一符号表** `g_main_elfsyms`（`main/elfSdk/elf_symtab.c`），由 `tools/gen_elf_syms.py` 从主固件 `.elf` 生成：

- **提取**：`riscv32-esp-elf-nm --defined-only -g`（含 WEAK 的 W/w 类型，覆盖 weak 的 operator new / `__cxa_*`；组件 `tool/symbols.py` 的 readelf 方案漏 WEAK，故自建）
- **白名单过滤**（脚本顶部，可读）：
  - C 函数：前缀（`lv_`/`esp_log_`/`cJSON_`/`esp_lv_adapter_`/`jpeg_`/`esp_jpeg_`）+ 精确名（`snprintf`/math/`rand`）
  - **网络符号（Phase 3）**：`lwip_`（socket 族，宏映射 `socket()`→`lwip_socket`/`close()`→`lwip_close`/`inet_pton`→`lwip_inet_pton` 等）+ `httpd_`（esp_http_server WS API）+ `esp_websocket_`（客户端）+ `xQueue`（FreeRTOS 队列，宏 `xQueueSend`→`xQueueGenericSend`）
  - C++ 类方法（`_Z*`）：`c++filt` 反解后匹配 `^Class::`（App/Task/Thread/GUI/FontLoader/Audio/单例…），**单例直出** = `Xxx::instance().method()`，.so 像原生代码
  - **C++ 文件级函数（Phase 3）**：`CPP_FN_PREFIXES` 按 demangled 前缀收（wsServer 5 函数是 C++ mangled 且无类名，`^Class::` 匹配不到）——同 Phase 2 加 `std::` 前缀的成熟模式
  - C++ 运行时：精确 mangled 名（`_Znwj`/`__cxa_*`）+ demangled 前缀（`operator new/delete`、`__cxa_`）
  - **数据符号（Phase 3）**：`DATA_EXACT` 白名单（`__dso_handle` 是 D 类型，脚本默认只收函数 T/t/W/w/A 收不到）——nm 提取时对 D/d/B/b/R/r 类型仅放行白名单内符号
- **生成** `{ "name", (void*)&name }`：`&name` **链接期取址**（主固件重编自动更新）；C++ mangled 名是合法 C 标识符，`extern int` 声明仅取址（链接器按字符串名匹配）
- **MUST_HAVE 校验**：核心符号缺失/未选中 → 脚本报错退出（防表缺关键符号致 .so 解析失败）
- ⚠ **static inline 的 LVGL API 不在 .elf**（调用时内联）→ 不导出；.so include `lvgl.h` 自行内联（无需符号解析）

**已删除的旧 4 张手写表**：`elf_export_syms.c` / `elf_cpp_runtime.cpp` / `elf_framework_syms.cpp` / `elf_sdk.cpp` 的 C 包装（`elf_task_*`/`elf_battery_*`/`elf_gamepad_*`）——统一由脚本生成的单表替代。

**Phase 2 补充（std:: 运行时导出）**：snake 用 std::string（getAiContext 拼接）触发 libstdc++ 内部符号（`_M_replace_cold`/`__throw_out_of_range_fmt`），原白名单只导 `operator new/delete` + `__cxa_` 前缀 → dlopen `Can't find common`。修复：运行时选择加 `or dem.startswith("std::")`（现 1799 符号，+458 std::）。另补 FreeRTOS 前缀 `xTask`/`vTask` + 精确名 `__assert_func`/`time`/`memmove`。

- ⚠ **重生成符号表的"鸡生蛋"**：移除主固件某代码后，其独占的 inline 符号（snake 的 `AudioHandle::isPlaying`）被 `--gc-sections` 回收 → 陈旧表引用未定义符号 → 主固件**链接失败**。解法：手动摘除该条目 → 链接通过 → `gen_elf_syms.py` 重生成 → 重建。**每次迁移移除代码后都要重生成符号表**。

**演进**：插件需要新符号 → 白名单加类名/前缀 → 重跑 `gen_elf_syms.py` → 重编主固件（不再手写任何符号表）。

## 6. 已核实的 ABI 名 / 坑（nm 结论，riscv32-esp-elf g++ 14.2.0 Itanium）

> ⚠ **不要手推 mangled 名，一律 `nm` / `tools/gen_elf_syms.py --search <elf> <关键字>` 核实。**

- 32 位目标 `size_t`=unsigned int → 编码 `j`（`_Znwj`/`_Znaj`，**不是 `_Znwm`/`_Znam`**）
- 成员函数参数 `App*` → 替换符 `S_`（`_ZN3App7pushAppEPS_`，不是 `EP3App`）
- 类名长度要数准：`AppStackManager`=15、`popApp`=6
- const 成员 `K` 紧跟 `_ZN`（`_ZNK...`）；参数编码前有 `E`（`_ZNK14VirtualGamepad8getStateEaa`）
- C++14 **sized delete** `_ZdlPvj`（.so 会引用，需导出）
- FreeRTOS 时序符号 `vTaskDelay`/`xTaskGetTickCount` 等（C 名，`xTask`/`vTask` 前缀导出）；libstdc++ 内部 `std::` 前缀（`_M_replace_cold`/`__throw_*`）需导出
- loader 内置 libc 表**没有** `snprintf`/`memcmp` → 需自导出
- `ESP_LOGI` 宏会引用 `esp_log`/`esp_log_timestamp`（App 内联虚函数 `onGamepadConnected/Disconnected` 里也有 ESP_LOGI，插件继承编译会带出）→ 需导出
- ⚠ **`__dso_handle`（Phase 3 实测）**：`.so` 用 `-nostdlib` 链接（不链接 crtbeginS.o）→ GNU ld 对 shared 目标 `__dso_handle` 引用做**硬检查**（`undefined reference`，不是可留空 UND）。触发条件：.so 内带析构的函数局部 static（如 `getS()` 里 `static S inst` 含 `std::function`）→ 引用 `__cxa_atexit(..., &inst, __dso_handle)`。**插件自足**：每个插件工程加 `dsoHandle.c`（`void* __dso_handle = (void*)&__dso_handle;`，等价 crtbeginS 惯例）。⚠ 勿在主固件自定义（ESP-IDF cxx 组件 cxx_guards.cpp 已定义 → multiple definition）；主固件侧 `DATA_EXACT` 导出 + `--undefined` 保活作双保险
- ⚠ **check_elf_syms.sh 拦不住白名单漏类**：脚本对照主固件 `.elf`（有定义→绿），但运行时解析用符号表 `g_main_elfsyms`（白名单过滤）——两个不同集合。白名单漏类只能运行时炸 `Can't find common`（tetris 漏 `HostManager`/`BleSettingsApp` 实测）。**迁移后应跑"UND vs 符号表"预检**（`nm -u <so>` 对照 `g_main_elfsyms` 而非 .elf）
- 已核实正确（无需改）：`_ZN3AppC1/C2EP7Display`、`_ZN3AppD1/D2Ev`、`_ZN14VirtualGamepad8instanceEv`、`_ZN10BleGamepad8instanceEv`、`_ZNK10BleGamepad14connectedCountEv`、`_ZN14BatteryManager{10getPercent,12getVoltageMv,10isCharging}Ev`

## 7. ElfManager（`main/elfManager/`）

- 缓存：path → { handle, info, refcount, size, mtime }
- `ensureLoaded`：持久内部 RAM 栈任务 `dlopen` + dlsym + 版本校验；检测 size/mtime 变化 → refcount==0 时重载（热更新）
- `createApp`：refcount++ → `info->create_app` → `App::setElfModule`
- `onAppDestroyed`：AppStack 在 `delete app` 后调用 → refcount-- → 0 时 `dlclose`
- 状态查询（TestApp 面板用）：isLoaded/path/name/version/refcount/loadTimeMs/moduleSize

## 8. 独立 .so 工程（C++ 构建，模板 `elfapps/smoke/`）

- 根 `CMakeLists.txt`：`EXTRA_COMPONENT_DIRS` 复用主工程 managed_components（elf_loader / cmake_utilities / lvgl / esp_lvgl_adapter）+ `project(smoke)` + **自定义 .so 目标**（`project_so` 只收 *.c，不支持 C++）：
  - `smoke.cpp` 作为**正常组件**编译（IDF 组件系统自动解析 REQUIRES 传递依赖，含 esp_lcd 等）+ `-fPIC -fno-exceptions -fno-rtti`
  - 用 `--whole-archive` 把 `__idf_main`（libmain.a，只含该 .so 的 .o）链接成 `.so`
  - 链接 flags：`-shared -fPIC -static-libgcc -nostdlib -nostartfiles` + `-fdata-sections -ffunction-sections -Wl,--gc-sections -fvisibility=hidden`（入口 `elf_plugin_entry` 加 `__attribute__((visibility("default")))`）
- `main/CMakeLists.txt`：`PRIV_INCLUDE_DIRS` 指向 `../../main` + `../../main/elfSdk`（app.hpp / elf_sdk.h），`REQUIRES lvgl esp_lvgl_adapter`
- 构建：`tools/build_elf.sh <name>`（`cd elfapps/<name> && idf.py so` → `build/<name>.so`）——**必须用 Ninja**（默认）
  - ⚠ 勿用 Unix Makefiles：对"自定义 .so 目标 → libmain.a 跨目录依赖"有缺陷（子 make 只读 so.dir/build.make，不含子目录 libmain.a 规则；smoke 能过仅因 libmain.a 早已存在）
  - ⚠ 换生成器/改 sdkconfig.defaults 后须删旧 `build/` 与 `sdkconfig`（defaults 不覆盖已存在值）
- **音频组件**：插件用 `Audio`（snakeRoom.hpp → audio/Audio.hpp → esp_audio_render/esp_audio_dec/esp_gmf_pool）→ REQUIRES + EXTRA_COMPONENT_DIRS 复用主工程 managed_components 的 6 个音频组件（esp_audio_render/gmf_audio/esp_audio_codec/esp_audio_effects/gmf_core/gmf_fft），仅头文件、.so 不链接音频代码
- **`dsoHandle.c`（Phase 3 起必备）**：每个插件工程 `main/` 下加 `void* __dso_handle = (void*)&__dso_handle;` 进 SRCS——`.so` `-nostdlib` 链接对 shared 目标的 `__dso_handle` 引用做硬检查（不链接 crtbeginS.o）；带析构的函数局部 static 必触发。tetris/douDiZhu 已加（smoke/snake/chineseChess/fruitNinja 的函数局部 static 无析构，未触发，但新工程一律加）
- **`CONFIG_HTTPD_WS_SUPPORT`（Phase 3 起注意）**：插件用 `httpd_ws_*`（WS 服务器 API）须在 `sdkconfig.defaults` + `sdkconfig` 加 `CONFIG_HTTPD_WS_SUPPORT=y`——`esp_http_server.h` 用 `#ifdef CONFIG_HTTPD_WS_SUPPORT` 包裹这些声明，插件独立工程默认关（tetris_net.cpp 报未声明实测）。主固件已开
- 上传：设备 HTTP 服务器文件管理界面（PUT → `/root/elf/<name>.so`；原 `upload_elf.sh` 已移除）

## 9. P4 关键坑

1. **组件管理**：独立 .so 工程必须用 `EXTRA_COMPONENT_DIRS` 复用主工程 managed_components（组件管理器对独立工程不可靠）。
2. **加载任务栈须在内部 RAM**：`esp_elf_arch_flush` 调 `spi_flash_disable_interrupts_caches_and_other_cpu()`，assert 栈在内部 RAM。⚠ **dlclose 无 arch_flush**（`esp_elf_deinit` 只 free），普通 PSRAM 栈可跑。
3. **PMP 使 PSRAM heap 不可执行（核心）**：`CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=y` → ELF 代码取指 `Instruction access fault`。**必须关闭**。
4. **include 依赖**：.so 编译**不能手拼 `-I`**（`COMPILE_INCLUDE_DIRECTORIES` 不含组件 `PRIV_INCLUDE_DIRS`，且追不完 esp_lcd→esp_driver_*→freertos 传递链）→ 必须走组件系统编译。

## 10. 工具链（`tools/`）

| 工具 | 用途 |
|------|------|
| `build_elf.sh <name>` | 构建 `elfapps/<name>` → `build/<name>.so` |
| `gen_elf_syms.py <elf>` | 从 .elf 生成符号表（白名单 + MUST_HAVE 校验） |
| `gen_elf_syms.py --search <elf> <关键字>` | 搜主固件实际 mangled 名（核对白名单/修正编码） |
| `riscv32-esp-elf-nm -u build/<name>.so` | 列插件未定义符号，对照导出表补齐 |

## 11. TestApp — ELF 测试台（`main/app/testApp/`）

保留 GT911 多指可视化，新增 ELF 测试面板（左上卡片）：
- 状态区：module / version / refcount / loadTimeMs / size
- 按钮（触摸 + 手柄 X=加载/A=运行/Y=重载/B=返回）：`[加载] [运行] [卸载] [重载]`
- `[运行]` → `push(ElfManager::createApp("/root/elf/smoke.so", ...))` → SmokeApp 全屏前台，B 退出回 TestApp 并刷新 refcount/dlclose

## 12. 插件编写规则

- **禁止文件级非 POD static**：loader **不执行 `.init_array`** → 文件级对象不被构造（=UB）；constexpr/POD static 安全（进 .rodata）；非 POD 用**函数局部 static**（`__cxa_guard_acquire` 运行时执行）或堆。
- ⚠ **文件级 `static struct {...} s` 的坑（Phase 3 tetris 实测）**：不仅不被构造，**默认成员初始化器也不生效**（BSS 零初始化 → `nextPlayerId = 1`/`listenSock = -1`/`myPlayerId = -1` 全变 0 = 运行时 bug）；且含 `std::function` 时 `.so` 链接报 `__dso_handle` undefined。**规范改法**：
  ```cpp
  struct S { int nextPlayerId = 1; std::function<void()> cb; };
  static S& getS() { static S inst; return inst; }   // 函数局部 static，首次调用真构造
  // 所有 s.xxx → getS().xxx
  ```
  （tetris 两个 net 文件 113 处 `s.` 机械替换实测可行）
- 编译 `-fno-exceptions -fno-rtti`；与主固件**同头文件 + 同 sdkconfig** + `ELF_SDK_VERSION` 守卫（防类布局漂移）。
- ⚠ **sdkconfig 必须同步主固件时序/ABI 配置**：`CONFIG_FREERTOS_HZ=1000`（主固件）vs 插件默认 100 → `.so` 内 `pdMS_TO_TICKS(80)`=8 ticks，运行时 1000Hz 下 `vTaskDelay(8)`=8ms → **游戏快 10 倍**（snake 实测踩坑）。插件 sdkconfig.defaults 已加 `CONFIG_FREERTOS_HZ=1000`；用 `httpd_ws_*` 另加 `CONFIG_HTTPD_WS_SUPPORT=y`
- 单例/框架方法跨边界走**单符号表**（`g_main_elfsyms`，脚本生成）；std::string 跨边界返回同工具链可行（AI 集成相关延后）。
- ⚠ **.so 体积控制（2026-08-09 决策，方案 B 构建后 strip，sdkconfig 驱动）**：`.so` 巨大（500K~1M）根因是 **IDF 全局注入 `-ggdb -gdwarf-4`**（`tools/cmake/build.cmake` 硬编码，注释明说 "always generate debug symbols (even in release mode...)"），且**无任何 sdkconfig 选项能关闭**（Compiler options 菜单只有 -O 级别/断言/杂项）；对普通固件调试信息不进 .bin 零成本，但 `.so` 文件本身就是产物 → 调试信息占文件 50-70%（douDiZhu 877KB → strip 后 97KB）。改 `CONFIG_COMPILER_OPTIMIZATION_{PERF,SIZE}` 只动代码段（O2 反而更大），**打不掉大头**。**方案 B（最终形态）：CMakeLists 不碰，`build_elf.sh` 构建后 strip，是否 strip 由插件 sdkconfig 优化级别 choice 自动决定**：`DEBUG=y`/`NONE=y`（-Og/-O0）→ 保留调试信息；`SIZE=y`/`PERF=y`（-Os/-O2）→ `riscv32-esp-elf-strip --strip-debug`。sdkconfig 是唯一事实来源，无需命令行开关（早期 `--debug` 参数已删）。`.dynsym` 不受 strip 影响，dlsym 安全（elf_loader 的 `elf->symtab` 只从 `.dynsym` global func 构建，esp_elf.c:643）；`.symtab` 保留，`nm -u` 符号检查不受影响。备选：链接期 `-Wl,--strip-debug`（脚本要开关就得改 CMake，麻烦，弃）或方案 A（`idf_build_replace_option_from_property` 编译期移除 `-ggdb`，隐藏 API，弃）。**代价**：release 插件无源码级调试（崩溃只剩裸地址）、addr2line 反解不可用；`ESP_LOGI` 运行时日志不受影响。**调试方法**：把插件 sdkconfig 优化级别改为 DEBUG 重编（自动保留调试信息），或重建时临时改回。

## 13. 测试计划 + 结果

### R1（✅ 已完成）— App* 模型全链路

| # | 验证点 | 结果 |
|---|--------|------|
| T0 | dlopen/dlsym/版本校验/失败路径 | ✅ dlopen OK，version=1 匹配 |
| T1 | C++ 运行时（std::vector/string + new/delete → PSRAM） | ✅ stdlib=PASS |
| T2 | 静态对象规则（文件级 vs 函数局部 vs constexpr） | ✅ fnLocal=PASS, fileStatic=0x0 |
| T3 | LVGL 渲染 + 符号解析 | ✅ 屏幕显示 SMOKE |
| T4 | 虚分派生命周期（init/onForeground/onBackground/deinit）+ 输入 | ✅ 全日志齐全 |
| T5 | 退出 → delete → refcount=0 → dlclose | ✅ 干净卸载 |
| 失败路径 | 无 .so / 版本不匹配 / 符号缺失 | ✅ 均优雅报错不崩 |

### R2（✅ 已完成，除可选测量）

- [x] 热更新实测：改 smoke 文案 → 重编 → PUT → 重进见新，主固件未重烧（用户实测 ✅）
- [x] 单例直出启用：smoke P7 调 Task::addTask/removeTask + BatteryManager::getPercent + BleGamepad::connectedCount/getBatteryLevel（设备验证通过 ✅；符号表已脚本化，无需手写）
- [ ] 卸载后 PSRAM 堆回落测量（`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`）— 可选
- [ ] 多 App 共享 .so（refcount=2，最后一个退出才 dlclose）— 待后续迁移自然覆盖
- [x] 无 .so 时入口"未安装"不崩溃（snake 实测：桌面卡片 0 缺 .so 优雅提示不崩 ✅）

### Phase 2 — snake 迁移（✅ 已完成）

| # | 验证点 | 结果 |
|---|--------|------|
| S1 | 插件构建（Ninja 生成器） | ✅ snake.so 构建成功 |
| S2 | dlopen + createApp | ✅ dlopen OK / createApp OK / refcount=1 |
| S3 | std:: 运行时符号解析 | ✅ 1799 符号（std:: 前缀补齐） |
| S4 | 游戏表现与原生一致 | ✅ 无差异（FREERTOS_HZ=1000 同步后） |
| S5 | 退出 → refcount=0 → dlclose | ✅ 无泄漏 |
| S6 | AI open_app("snake") + 上下文 | ✅ 正常（createSnakePlugin 适配器） |
| S7 | 无 .so 兜底 | ✅ 优雅提示不崩 |
| S8 | newApp 清理 | ✅ 已删（AI 路径不走它） |

### Phase 3 — 联机游戏迁移（tetris / douDiZhu，✅ 已完成）

| # | 验证点 | 结果 |
|---|--------|------|
| N1 | 网络符号导出 | ✅ lwip 51/httpd 46/esp_websocket 9/xQueue 20/wsServer 5/__errno，共 2133 符号 |
| N2 | HTTPD_WS_SUPPORT | ✅ tetris sdkconfig 加 `CONFIG_HTTPD_WS_SUPPORT=y`（httpd_ws_* 未声明修复） |
| N3 | tetris dlopen + 运行 | ✅ WS 玩家加入 /ws/tetris + TCP 8083 + send task 20fps + HostManager 槽位 |
| N4 | tetris 退出卸载 | ✅ refcount=0 → dlclose 干净 |
| N5 | douDiZhu dlopen + 单机 AI | ✅ 1332ms/844KB → createApp → 单机 AI 模式正常 → 退出 dlclose 干净 |
| N6 | douDiZhu 联机 | ⏳ 待测（需第二块 P4：主机连从机 3 人对战 + 插件内 AI 引擎） |
| N7 | 回归（旧插件） | ✅ snake/chineseChess/fruitNinja 符号表重构后仍正常 |
| N8 | AI 动态路由 | ✅ open_app("tetris"/"doudizhu") 走 elfPath 正常 |
| N9 | 主固件瘦身 | ✅ 4.79MB（tetris/douDiZhu 迁出） |

### 多轮迭代原则

主固件（TestApp 测试台）只编一次；smoke.so 每轮热更新演进（改探测点重编 + PUT 即生效）。每轮 = 一个里程碑，独立可跑、结果可记录，不追求一轮全绿。

## 14. 路线图

- **Phase 2**：迁移单机游戏。**snake ✅ + chineseChess ✅（含 AI 引擎）+ fruitNinja ✅（canvas 渲染器最重）——全部完成，设备验证通过**。迁移流程模板化：`git mv` 源文件 → 插件工程（CMakeLists + plugin.cpp + sdkconfig.defaults FREERTOS_HZ=1000）→ 主固件删源 + desktopApp 卡片/AI 路由 → 重生成符号表 → build_elf.sh（自动符号检查）→ PUT 上传。
- **Phase 3**：迁移联机游戏（tetris/douDiZhu）。**决策：导出全套网络符号**（socket 族 + wsServer + httpd_ws + esp_websocket_client，插件像原生代码）。**✅ 全部完成（2026-08-09）**：网络符号面（lwip_/httpd_/esp_websocket_/xQueue 前缀 + wsServer CPP_FN + __dso_handle DATA_EXACT）、tetris 整文件夹迁移（含 TetrisRemoteApp 从机模式）、douDiZhu 迁移；设备验证通过（tetris WS/TCP 联机 ✅、douDiZhu 单机 AI ✅）；**douDiZhu 联机待测（需两块 P4）**。
- **Phase 4**：桌面动态扫描 `/root/elf/*.so`；主固件瘦身（5MB→~2.5-3MB）；删 `elfProbe`；market 分发 .so（可选）；文档回填（newAGENTS/newreadme）。
  - **✅ 已完成（2026-08-08，代码+主固件构建通过，待设备验证）**：ABI v2 元数据字段、ElfManager 多模块 map + listElfFiles/inspectModule、桌面动态卡片（后台渐进式扫描）、AI 能力动态合并（AiAppTarget.elfPath）、`setElfModule` 指针→path。
  - ⏳ 待验证：重编 snake/smoke 插件（补元数据字段）→ 上传 → 设备验证清单（见下）。**✅ 已完成（含 fruitNinja/chineseChess）**
  - ✅ 已完成：删 `elfProbe`（已从 CMakeLists + main.cpp 移除）、删 `appFactory`（未编译死代码）、删 `elfapps/hello`（纯 C 原型）
  - ⏳ 待做：主固件瘦身（tetris/douDiZhu 已迁 → 现 4.79MB，**可评估再删 Lua/市场等系统 App**）、删 `elfapps/smoke`（测试探针，确认无用后）、AGENTS.md/readme 回填
  - ✅ **已完成（2026-08-09）：.so 体积瘦身**——`build_elf.sh` 读插件 sdkconfig 优化级别 choice 自动决定：`DEBUG=y`/`NONE=y`（-Og/-O0）→ 保留调试信息；`SIZE=y`/`PERF=y`（-Os/-O2）→ 构建后 `--strip-debug`（无命令行开关，sdkconfig 为唯一事实来源）。**5 个插件 sdkconfig 已全部切 `CONFIG_COMPILER_OPTIMIZATION_PERF=y`（-O2）并重建，全部瘦身完成**：douDiZhu 877KB→97KB（-89%），其余各 .so 均 <100KB；设备验证运行正常（dlopen/游戏/退出 dlclose 均无异常）。验证方法：`riscv32-esp-elf-size -A` 确认 .debug_* 消失；dlopen 日志加载 ms 显著下降（`esp_elf_open` 全文件读入 RAM，文件越小加载越快）。

## 15. 相关文件清单

| 文件 | 说明 |
|------|------|
| `main/elfSdk/elf_sdk.h` | ABI 契约（`elf_plugin_info_t` + `ELF_SDK_VERSION` + `elf_sdk_init` 声明）。**v2：+display_name/description/icon_path** |
| `main/elfSdk/elf_sdk.cpp` | `elf_sdk_init()` 注册 `g_main_elfsyms`（单表） |
| `main/elfSdk/elf_symtab.c` | **符号表（脚本生成）**：`g_main_elfsyms`，tools/gen_elf_syms.py 白名单生成 |
| `tools/gen_elf_syms.py` | 符号表生成脚本（nm + c++filt + 白名单 + MUST_HAVE 校验 + `--empty` 空表占位 + 出错输出空表） |
| `tools/check_elf_syms.sh` | **符号检查**：`.so` UND（含弱符号）对照 `.elf`，build_elf.sh 自动调用 |
| `main/CMakeLists.txt` | **符号保活**：`target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=<sym>")`（须 INTERFACE，静态库 PRIVATE 失效） |
| `main/elfManager/elfManager.hpp/.cpp` | **多模块 map**：path→Module；listElfFiles/inspectModule 扫描；双层锁粒度 |
| `main/app/app.hpp` | App 基类 + `setElfModule(path)`/`m_elfModulePath[64]` + `AiAppTarget.elfPath` |
| `main/app/appStack.cpp` | 两处删除点 `delete` 后按 path 调 `onAppDestroyed` |
| `main/app/desktopApp/desktopApp.hpp/.cpp` | **动态卡片**：m_cards/m_cardObjs + 后台 ELF 扫描 + 按 kind 分发（**内置卡片已全清**，CardKind 只剩 Elf/System） |
| `main/aiAgent/aiExecutor.hpp` | open_app：`t.elfPath ? ElfManager::createApp : t.create` |
| `main/app/testApp/testApp.hpp/.cpp` | ELF 测试面板（状态查询改按 path 传参） |
| `elfapps/smoke/` | C++ smoke 插件工程（CMakeLists + smoke.cpp，**v2 元数据已补**） |
| `elfapps/snake/` | **snake 插件工程（Phase 2 完成）**：main/snakeRoom/ + snakeGame/ + snakePlugin.cpp（**v2 元数据已补**） |
| `elfapps/fruitNinja/` | **fruitNinja 插件工程（Phase 2 完成）**：main/fruitNinjaRoom/ + App/ + Logic/ + Renderer/ + fruitNinjaPlugin.cpp（REQUIRES esp_driver_jpeg/esp_timer/触摸 ABI 契约） |
| `elfapps/chineseChess/` | **chineseChess 插件工程（Phase 2 完成）**：main/ 含 AI 引擎 + chineseChessPlugin.cpp（REQUIRES esp_timer） |
| `elfapps/tetris/` | **tetris 插件工程（Phase 3 完成）**：main/app/tetris/ 整文件夹（含 TetrisRemoteApp）+ tetrisPlugin.cpp + **dsoHandle.c** + sdkconfig.defaults（FREERTOS_HZ=1000 + HTTPD_WS_SUPPORT=y）；REQUIRES esp_http_server/esp_websocket_client/json/lwip + 音频组件 |
| `elfapps/douDiZhu/` | **douDiZhu 插件工程（Phase 3 完成）**：main/app/douDiZhu/ 整文件夹 + douDiZhuPlugin.cpp + **dsoHandle.c** + sdkconfig.defaults（FREERTOS_HZ=1000）；REQUIRES json/lwip（纯 TCP 无音频/WS） |
| `main/elfSdk/` | `gen_elf_syms.py` 白名单含网络前缀（lwip_/httpd_/esp_websocket_/xQueue）+ `CPP_FN_PREFIXES`（wsServer）+ `DATA_EXACT`（__dso_handle） |
| `main/CMakeLists.txt` | 符号保活含 `__dso_handle` + wsServerUnregister + lv_canvas 3 个 + margin 2 个 + AudioSample 3 个 + App::replaceWith + text_line_space（**9 个 GC 回收符号**） |
| `tools/build_elf.sh` / `gen_elf_syms.py` / `check_elf_syms.sh` | 构建插件 .so（Ninja；**读插件 sdkconfig 优化级别自动决定是否 strip-debug**——DEBUG/NONE 保留调试信息，SIZE/PERF 构建后 strip，sdkconfig 为唯一事实来源）+ 自动符号检查 / 符号表生成+校验+搜索（--empty 空表）/ .so↔.elf 符号对照 |

## 16. 开发日志（时间线）

- **2026-08-08**：设计确定（App* 工厂模型，替代容器）；R1 代码落地；依次解决：operator new 语法坑 → 5 个 App/单例 mangled 名（nm 核实）→ `size_t`=`j` → smoke .so 6 个 UND 符号 → **ElfManager::loadOne 把 dlsym 函数指针当结构体解引用**（假版本 1303）→ **R1 全绿 ✅**。
- **2026-08-08（Phase 1 符号面脚本化）**：4 张手写符号表 → **单表 `g_main_elfsyms`（gen_elf_syms.py 从 .elf 白名单生成）**；删 7 个 C 包装；主固件 build 通过。**关键验证**：① `extern int _ZN...;` + `&_ZN...` 对 C++ mangled 符号链接通过（假设成立）；② static inline 的 LVGL API 不在 .elf（`lv_obj_clear_flag` 等取址 undefined）→ 修正为过渡表 include `lvgl.h` 取址生成本地副本；最终脚本 nm 天然排除 inline，.so 自行内联。smoke P7 打开单例直出（Task/BatteryManager/BleGamepad），待设备验证。
- **2026-08-08（Phase 2 snake 迁移）**：用户把 `main/app/snake/` 移到 `elfapps/snake/main/`；主固件 desktopApp/appFactory 路由改 `ElfManager::createApp("/root/elf/snake.so", ...)`（AI 能力表 create 换静态适配器 `createSnakePlugin`）。依次解决：**① Unix Makefiles 生成器无法构建 .so**（自定义目标跨目录依赖 libmain.a 无规则）→ `build_elf.sh` 改 Ninja；**② 符号表鸡生蛋**（snake 独占 inline `AudioHandle::isPlaying` 被 GC → 陈旧表链接失败）→ 手动摘除 + 重生成；**③ std:: 运行时符号未导出**（`_M_replace_cold`/`__throw_out_of_range_fmt`）→ 白名单加 `std::` 前缀（1799 符号）；**④ 游戏快 10 倍**（插件 FREERTOS_HZ=100 与主固件 1000 不匹配）→ sdkconfig.defaults 同步 `CONFIG_FREERTOS_HZ=1000`。**设备全链路验证通过**（游戏表现与原生一致 / refcount=0→dlclose / AI open_app + 上下文 / 无 .so 兜底不崩）。清理 `SnakeRoom::newApp`（死代码）。
- **2026-08-08（Phase 4 桌面动态扫描，代码完成）**：ABI v2（`elf_plugin_info_t` +display_name/description/icon_path，`ELF_SDK_VERSION`→2）；ElfManager 单模块→多模块 map（`setElfModule` 指针→path，map 节点地址漂移坑）+ `listElfFiles`/`inspectModule` 扫描（dlopen→读→dlclose 不增 refcount）；桌面 `GAME_COUNT` 静态数组→动态 m_cards（ELF 在前+内置+系统）+ 后台渐进式扫描 Task；`AiAppTarget` +elfPath（aiExecutor 动态加载）；`getAiContext`/`getOpenableApps` 动态化；删 `createSnakePlugin` 适配器。编译坑：`/*` 注释内 `*.so` 触发 -Werror=comment、memset 非平凡类型、snprintf 截断（memcpy 手动拼）。**主固件构建通过（5.2MB）**；待重编 snake/smoke 插件 + 设备验证。
- **2026-08-08（Phase 2 完成：fruitNinja + chineseChess 迁移）**：用户拍板迁移顺序 **fruitNinja（先，canvas 最重）→ chineseChess（后）**，联机游戏暂缓。清理死代码（`elfProbe`/`appFactory`/`elfapps-hello`）。依次解决：**① 插件编译缺头文件**（`esp_lcd_touch_gt911.h`/`esp_timer.h`）→ REQUIRES 加触摸 ABI 契约组件（`esp_lcd_touch`+`esp_lcd_touch_gt911`，EXTRA_COMPONENT_DIRS 复用，依赖链均 IDF 自带）+ `esp_timer`/`esp_driver_jpeg`；**② 符号缺失逐个炸**（`lv_obj_set_style_transform_scale_y`/`sqrt`/`__floatdisf`/`__udivdi3`/`fseek`/`__clzsi2`）→ 建立**符号保活三层机制**：白名单补 `LIBGCC_PREFIXES`（`__clz/__ctz/__popcount` 等）+ double 数学精确名 + 文件 API；`target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=...")`（⚠ PRIVATE 在静态库上静默失效；`used,retain` 数组在 ESP-IDF 链接脚本下无效——节被 GC 整个消失）；**③ 构建时符号检查** `tools/check_elf_syms.sh`（`nm -u` 含弱符号对照 .elf，缺失即拦截）；**④ 鸡生蛋一键化** `gen_elf_syms.py --empty` 空表占位 + 出错也输出空表（`>` 重定向后文件永远合法）；**⑤ `__cxa_guard_abort`/`__cxa_pure_virtual` 被 GC** → 移出 MUST_HAVE。**设备全链路验证通过**：三个插件加载→运行→退出→refcount=0→dlclose 干净卸载；fruitNinja JPEG 硬件解码 5 水果图正常；桌面动态卡片自动出现；主固件 5.06MB。
- **2026-08-09（Phase 3 联机游戏迁移：tetris + douDiZhu，全部完成）**：用户 `git mv main/app/{tetris,douDiZhu} elfapps/<名>/main/app/`（目录对齐主固件 include 路径，13 处 `app/tetris/xxx` include 零改动）。**Phase 3A 网络符号面**：`gen_elf_syms.py` 白名单加 `lwip_`/`httpd_`/`esp_websocket_`/`xQueue` 前缀（lwip socket 宏映射 `LWIP_COMPAT_SOCKETS` 确认）+ `__errno` + **`CPP_FN_PREFIXES`**（wsServer 5 函数 C++ mangled 无类名）+ **`DATA_EXACT`**（`__dso_handle` D 类型数据符号，脚本只收函数类型收不到）；旧 .elf 模拟验证 2103 符号全选中。**tetris 迁移**：插件工程 REQUIRES esp_http_server/esp_websocket_client/json/lwip + 音频组件；主固件清 tetris 9 源 + desktop 卡片/AI 表 + 删 `BUILTIN_COUNT`（-Werror=switch 强制删 `CardKind::Builtin` 枚举）。**依次解决**：**① HTTPD_WS_SUPPORT 未开**（tetris_net.cpp 报 httpd_ws_* 未声明，esp_http_server.h 用 `#ifdef CONFIG_HTTPD_WS_SUPPORT` 包裹）→ sdkconfig.defaults + sdkconfig 加 `CONFIG_HTTPD_WS_SUPPORT=y`；**② 文件级 static struct 坑**（tetris 两 net 文件 `static struct {...} s` 含 std::function + 非零默认值 `nextPlayerId=1`/`listenSock=-1`，.so 链接报 `__dso_handle` undefined + loader 不执行 .init_array 默认值全变 0）→ 改 `struct S` + `static S& getS(){ static S inst; return inst; }`，113 处 `s.`→`getS().` 脚本机械替换；**③ __dso_handle 链接 vs 导出**：先在 elf_sdk.cpp 自定义 → 与 libcxx（cxx_guards.cpp:217）**重定义** → 撤销；正解是插件 .so 自足（`dsoHandle.c` 提供）+ 主固件 `DATA_EXACT` 导出双保险；**④ GC 回收符号 9 个**（lv_canvas×3 / margin×2 / AudioSample×3 / App::replaceWith —— tetris 迁出后唯一调用方消失）→ `--undefined` 保活；**⑤ 白名单漏类**（`HostManager`/`BleSettingsApp` 不在 CPP_CLASSES，check_elf_syms.sh 对照 .elf 拦不住，运行时炸 `Can't find common`）→ 补两个类名（2133 符号）。**douDiZhu 迁移**：纯 TCP（REQUIRES json/lwip），静态预检零缺口（调用的外部类全在白名单，ElfManager 只在注释，BidResult/Combo 等是自带枚举），仅 `lv_obj_set_style_text_line_space` 被 GC → 保活。**设备验证通过**：tetris WS 玩家加入 /ws/tetris + TCP 8083 + send task 20fps + HostManager 槽位正常 / douDiZhu 单机 AI 正常 / 均 refcount=0→dlclose 干净 / 旧插件回归正常 / AI open_app 动态路由正常 / 主固件 4.79MB。**douDiZhu 联机待测（需两块 P4）**。
- **2026-08-09（.so 体积诊断 + 方案 B 落地）**：用户发现 .so 巨大（tetris 969KB / douDiZhu 844KB / snake 682KB）而主固件只瘦 5.06→4.79MB。**诊断**：① 根因 = IDF 全局注入 `-ggdb -gdwarf-4`（`tools/cmake/build.cmake` 硬编码，对普通固件调试信息不进 .bin 零成本，但 .so 文件本身就是产物 → 调试信息占 50-70%）；② 主固件"没怎么变"是错觉——.bin 只含 loadable sections，主固件大头是系统库，游戏代码只占几百 KB；③ 优化级别（`CONFIG_COMPILER_OPTIMIZATION` choice）**只控制 -O**，`LEVEL_{DEBUG,RELEASE}`/`DEFAULT` 是 v5.5 新写法经 `sdkconfig.rename` 映射回同一 choice（非重复记录），改 O2/Os 动不了调试信息（用户实测 O2 844→877KB 反而更大）；④ 无 sdkconfig 选项能关 -g（Compiler options 菜单核实）。**影响**：`esp_elf_open` 全文件 `malloc(size)`+`read` 进 RAM（esp_elf.c:61）→ dlopen 峰值内存/时间与文件大小成正比（tetris 1332ms）。**决策演进**：先加链接期 `-Wl,--strip-debug`（5 个 CMakeLists）→ 用户提出调试需要开关但脚本不该改 CMake → **改为构建后 strip（C 路线）**：CMakeLists 恢复干净，`build_elf.sh` 默认 `riscv32-esp-elf-strip --strip-debug` + `--debug` 开关 → **再演进：sdkconfig 驱动**（用户提议 `--debug` 多余，读 sdkconfig 判断）：删 `--debug` 参数，`build_elf.sh` 按插件 sdkconfig 优化级别 choice 自动决定（DEBUG/NONE 保留，SIZE/PERF strip），sdkconfig 为唯一事实来源。**实测 douDiZhu（PERF）877KB→97KB（-89%）且运行正常**。**5 个插件 sdkconfig.defaults 全部加 `CONFIG_COMPILER_OPTIMIZATION_PERF=y`（-O2）并重建，全部瘦身完成（各 .so 均 <100KB），设备验证运行正常**。`.dynsym` 不受影响，dlsym 安全；`ESP_LOGI` 运行时日志不受影响，仅失去源码级调试/addr2line。
