# elfPlan.md — ELF 游戏插件化（开发中活文档）

> **本文件定位**：ELF 游戏插件化的**活的开发文档**（本分支专属，内部不分发）。所有设计决策、当前状态、ABI 结论、测试计划/结果、路线图都在这里高频更新；**架构稳定后**（如 Phase 2 迁完第一个游戏）再一次性回填到 `newAGENTS.md` / `newreadme.md`。
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
│   ├─ elf_export_syms.c     — C 库生成表     │   └─ 返回 elf_plugin_info_t
│   ├─ elf_cpp_runtime.cpp   — C++ 运行时表   │       { version, name, create_app, destroy_app }
│   ├─ elf_framework_syms.cpp— C++ 框架/单例  ├─ XxxElfApp : public App（.so 内 new）
│   └─ elf_sdk.cpp           — 注册 + C 包装  ├─ 自建事件循环/线程（原生灵活性）
├─ ElfManager                                  └─ 调用主固件符号（lockGuard/Task/单例…）
│   ├─ 内部 RAM 栈任务 dlopen + version 校验
│   ├─ refcount + size/mtime 热更新检测
│   └─ AppStack delete 后 → dlclose
└─ AppStack（对 ELF App 与原生 App 一视同仁）
```

- 主固件 `push(new App{params})` 换成 `push(ElfManager::createApp(path, display, params))`
- **AppStack 全权管理生命周期**：push/pop/init/deinit/onForeground/onBackground/onGamepadInput 全部虚分派进 .so（vtable 在 PSRAM）
- 插件与原生 App 完全一致：自建事件循环/线程、onGamepadInput 直收输入、display->lockGuard() 直接调用

## 3. 当前状态（2026-08-08：R1 全绿 ✅）

**实测证据（串口日志）**：

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

## 5. 符号导出（4 张表，`elf_sdk_init()` 在 main.cpp 启动早期注册）

| 表 | 文件 | 内容 |
|----|------|------|
| C 库生成表 | `main/elfSdk/elf_export_syms.c` | `lv_*` / `esp_log_*` / math / cJSON / `snprintf`/`memcmp` 等 C 函数（name==symbol，可 `tools/gen_elf_syms.py` 全量生成） |
| C++ 运行时表 | `main/elfSdk/elf_cpp_runtime.cpp` | operator new/delete（`_Znwj`/`_Znaj`/`_ZdlPv`/`_ZdaPv`/`_ZdlPvj`）、`__cxa_guard_*`、`__cxa_atexit`、`__cxa_pure_virtual`、throw 辅助（`__throw_bad_alloc`/`__throw_length_error`/`__throw_bad_array_new_length`/`__throw_logic_error`） |
| C++ 框架表 | `main/elfSdk/elf_framework_syms.cpp` | App ctor/dtor/pushApp/popApp/replaceWith/setManager + 单例直出（VirtualGamepad/BleGamepad/BatteryManager 的 instance()+方法，asm 重命名取址） |
| SDK C 包装 | `main/elfSdk/elf_sdk.cpp` | `elf_task_add`/`elf_task_remove`、`elf_battery_*`、`elf_gamepad_*`（稳定 C 名，规避 C++ mangling） |

**单例直出设计**：`instance()` + 方法直接注册，.so 像原生代码一样 `BleGamepad::instance().connectedCount()`。已核实所有单例 `instance()` 定义在 .cpp（非内联、安全）。

## 6. 已核实的 ABI 名 / 坑（nm 结论，riscv32-esp-elf g++ 14.2.0 Itanium）

> ⚠ **不要手推 mangled 名，一律 `nm` / `tools/check_elf_syms.sh` 核实。**

- 32 位目标 `size_t`=unsigned int → 编码 `j`（`_Znwj`/`_Znaj`，**不是 `_Znwm`/`_Znam`**）
- 成员函数参数 `App*` → 替换符 `S_`（`_ZN3App7pushAppEPS_`，不是 `EP3App`）
- 类名长度要数准：`AppStackManager`=15、`popApp`=6
- const 成员 `K` 紧跟 `_ZN`（`_ZNK...`）；参数编码前有 `E`（`_ZNK14VirtualGamepad8getStateEaa`）
- C++14 **sized delete** `_ZdlPvj`（.so 会引用，需导出）
- loader 内置 libc 表**没有** `snprintf`/`memcmp` → 需自导出
- `ESP_LOGI` 宏会引用 `esp_log`/`esp_log_timestamp`（App 内联虚函数 `onGamepadConnected/Disconnected` 里也有 ESP_LOGI，插件继承编译会带出）→ 需导出
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
- 构建：`tools/build_elf.sh <name>`（`cd elfapps/<name> && idf.py -G 'Unix Makefiles' so` → `build/<name>.so`）
- 上传：`tools/upload_elf.sh <name> <ip>`（HTTP PUT → `/root/elf/<name>.so`，复用主固件 `httpPut`，上限 10MB）

## 9. P4 关键坑

1. **组件管理**：独立 .so 工程必须用 `EXTRA_COMPONENT_DIRS` 复用主工程 managed_components（组件管理器对独立工程不可靠）。
2. **加载任务栈须在内部 RAM**：`esp_elf_arch_flush` 调 `spi_flash_disable_interrupts_caches_and_other_cpu()`，assert 栈在内部 RAM。⚠ **dlclose 无 arch_flush**（`esp_elf_deinit` 只 free），普通 PSRAM 栈可跑。
3. **PMP 使 PSRAM heap 不可执行（核心）**：`CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=y` → ELF 代码取指 `Instruction access fault`。**必须关闭**。
4. **include 依赖**：.so 编译**不能手拼 `-I`**（`COMPILE_INCLUDE_DIRECTORIES` 不含组件 `PRIV_INCLUDE_DIRS`，且追不完 esp_lcd→esp_driver_*→freertos 传递链）→ 必须走组件系统编译。

## 10. 工具链（`tools/`）

| 工具 | 用途 |
|------|------|
| `build_elf.sh <name>` | 构建 `elfapps/<name>` → `build/<name>.so` |
| `upload_elf.sh <name> <ip>` | HTTP PUT 上传 → `/root/elf/<name>.so` |
| `check_elf_syms.sh <elf>` | 校验主固件 .elf 中符号表所需全部符号存在（ABI 名核对）；`<elf> <关键字>` 搜实际 mangled 名 |
| `gen_elf_syms.py <elf>` | 从 .elf 生成完整 C 库导出表（替换 `elf_export_syms.c`） |
| `riscv32-esp-elf-nm -u build/<name>.so` | 列插件未定义符号，对照导出表补齐 |

## 11. TestApp — ELF 测试台（`main/app/testApp/`）

保留 GT911 多指可视化，新增 ELF 测试面板（左上卡片）：
- 状态区：module / version / refcount / loadTimeMs / size
- 按钮（触摸 + 手柄 X=加载/A=运行/Y=重载/B=返回）：`[加载] [运行] [卸载] [重载]`
- `[运行]` → `push(ElfManager::createApp("/root/elf/smoke.so", ...))` → SmokeApp 全屏前台，B 退出回 TestApp 并刷新 refcount/dlclose

## 12. 插件编写规则

- **禁止文件级非 POD static**：loader **不执行 `.init_array`** → 文件级对象不被构造（=UB）；constexpr/POD static 安全（进 .rodata）；非 POD 用**函数局部 static**（`__cxa_guard_acquire` 运行时执行）或堆。
- 编译 `-fno-exceptions -fno-rtti`；与主固件**同头文件 + 同 sdkconfig** + `ELF_SDK_VERSION` 守卫（防类布局漂移）。
- 单例/框架方法跨边界走符号注册（4 张表）；std::string 跨边界返回同工具链可行（AI 集成相关延后）。

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

### R2（待办）

- [ ] 热更新实测：改 smoke 文案 → 重编 → PUT → 重进见新，主固件未重烧
- [ ] 单例直出启用：smoke 加回 VirtualGamepad/BleGamepad/BatteryManager 调用（符号已全 ✅）
- [ ] 卸载后 PSRAM 堆回落测量（`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`）
- [ ] 多 App 共享 .so（refcount=2，最后一个退出才 dlclose）
- [ ] 无 .so 时入口"未安装"不崩溃

### 多轮迭代原则

主固件（TestApp 测试台）只编一次；smoke.so 每轮热更新演进（改探测点重编 + PUT 即生效）。每轮 = 一个里程碑，独立可跑、结果可记录，不追求一轮全绿。

## 14. 路线图

- **Phase 2**：迁移单机游戏（snake → chineseChess → fruitNinja），编译目标从 main 改 elfapps + 工厂导出，用 `nm -u` 补齐框架表（GUI::/Task::addTask/FontLoader 等新符号）。
- **Phase 3**：迁移联机游戏（tetris/douDiZhu，lwip socket 已导出 + WS/TCP 桥接决策：SDK `elf_ws_register` vs 插件自开端口）。
- **Phase 4**：桌面动态扫描 `/root/elf/*.so`；主固件瘦身（5MB→~2.5-3MB）；删 `elfProbe`；market 分发 .so（可选）；文档回填（newAGENTS/newreadme）。

## 15. 相关文件清单

| 文件 | 说明 |
|------|------|
| `main/elfSdk/elf_sdk.h` | ABI 契约（`elf_plugin_info_t` + `ELF_SDK_VERSION` + SDK C 包装声明） |
| `main/elfSdk/elf_sdk.cpp` | `elf_sdk_init()` 注册 4 张表 + SDK C 包装实现 |
| `main/elfSdk/elf_cpp_runtime.cpp` | C++ 运行时符号表（operator new/delete 用包装函数规避语法坑） |
| `main/elfSdk/elf_framework_syms.cpp` | C++ 框架/单例符号表（asm 重命名取址） |
| `main/elfSdk/elf_export_syms.c` | C 库符号表（R1 手工最小集，可 gen_elf_syms.py 全量替换） |
| `main/elfManager/elfManager.hpp/.cpp` | 模块缓存/refcount/热更新/内部 RAM 加载 worker |
| `main/app/app.hpp` | App 基类 + `setElfModule`/`m_elfModule` |
| `main/app/appStack.cpp` | 两处删除点 `delete` 后调 `onAppDestroyed` |
| `main/app/testApp/testApp.hpp/.cpp` | ELF 测试面板 |
| `elfapps/smoke/` | C++ smoke 插件工程（CMakeLists + smoke.cpp） |
| `tools/build_elf.sh` / `upload_elf.sh` / `check_elf_syms.sh` / `gen_elf_syms.py` | 构建/上传/校验/生成脚本 |

## 16. 开发日志（时间线）

- **2026-08-08**：设计确定（App* 工厂模型，替代容器）；R1 代码落地；依次解决：operator new 语法坑 → 5 个 App/单例 mangled 名（nm 核实）→ `size_t`=`j` → smoke .so 6 个 UND 符号 → **ElfManager::loadOne 把 dlsym 函数指针当结构体解引用**（假版本 1303）→ **R1 全绿 ✅**。
