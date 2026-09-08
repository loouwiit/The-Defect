# ELF 插件开发指南

## 概述

The-Defect 的游戏采用 **ELF 插件架构**：每个游戏编译为独立的 `.so` 动态库，放到 FAT 分区的 `/root/elf/<name>/` 目录下，运行时由 `dlopen` 动态加载。

**优势**：
- 改一个游戏只重编译该游戏，无需重烧主固件
- 游戏运行在独立 `.so` 中，可自建线程/事件循环
- 热更新：编译后 HTTP PUT 上传即可生效
- 主固件与游戏解耦，独立迭代

## 插件包结构

```
/root/elf/<name>/
├── manifest.json    ← 元数据（名称、描述、图标路径）
├── <name>.so        ← 编译后的动态库
└── icon.jpg         ← 桌面卡片图标
```

`manifest.json` 示例：
```json
{
  "name": "snake",
  "display_name": "贪吃蛇",
  "description": "经典贪吃蛇游戏",
  "main": "snake.so",
  "icon": "icon.jpg"
}
```

## 快速创建新插件

### 1. 创建目录结构

```
elfapps/<name>/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── <name>ElfApp.hpp
│   ├── <name>ElfApp.cpp
│   └── dsoHandle.c
├── sdkconfig.defaults
├── plugin_meta.json
└── icon.jpg
```

### 2. 唯一 C 入口

每个插件必须导出一个 C 函数：

```cpp
// <name>ElfApp.cpp
extern "C" {

#include "elfSdk/elf_sdk.h"

static void* create_app(void* display, const void* cjson_params) {
    // display = Display* 指针
    // cjson_params = cJSON* 参数（可选）
    return new YourElfApp(static_cast<Display*>(display));
}

static void destroy_app(void* app) {
    delete static_cast<App*>(app);
}

__attribute__((visibility("default")))
const elf_plugin_info_t* elf_plugin_entry(void) {
    static const elf_plugin_info_t info = {
        .version = ELF_SDK_VERSION,
        .create_app = create_app,
        .destroy_app = destroy_app,
    };
    return &info;
}

}  // extern "C"
```

### 3. 继承 App 基类

```cpp
#pragma once
#include "app/app.hpp"

class YourElfApp : public App {
public:
    YourElfApp(Display* display) : App(display) {}
    ~YourElfApp() override;

    void init() override;
    void deinit() override;
    void onForeground() override;
    void onBackground() override;
    void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;
};
```

### 4. dsoHandle.c

```c
// 必须提供，-nostdlib 链接时需要
void* __dso_handle = (void*)0;
```

### 5. CMakeLists.txt（插件工程）

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../main")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<name>_plugin)
```

### 6. main/CMakeLists.txt

```cmake
idf_component_register(
    SRCS "<name>ElfApp.cpp" "dsoHandle.c"
    INCLUDE_DIRS "."
    REQUIRES main
)

# 关键编译选项
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fPIC -fno-exceptions -fno-rtti -fvisibility=hidden
)
target_link_options(${COMPONENT_LIB} PRIVATE
    -nostdlib -shared
    -Wl,--whole-archive $<TARGET_FILE:main> -Wl,--no-whole-archive
)
```

### 7. sdkconfig.defaults

```ini
# 必须与主固件同步！
CONFIG_FREERTOS_HZ=1000
```

## 构建与上传

```bash
# 构建
tools/build_elf.sh <name>

# 产物在 build/pkg/ 下
ls build/pkg/
# <name>.so  manifest.json  icon.jpg

# 上传到设备
curl -T build/pkg/<name>.so http://<主机名>.local/elf/<name>/<name>.so
curl -T build/pkg/manifest.json http://<主机名>.local/elf/<name>/manifest.json
curl -T build/pkg/icon.jpg http://<主机名>.local/elf/<name>/icon.jpg
```

上传后，返回桌面即可看到新游戏卡片（`onForeground → syncElfCards` 自动扫描）。

## 可用的主固件 API

插件通过符号表访问主固件提供的函数。主要类别：

### App 基类
- `App::pushApp()` / `popApp()` / `replaceWith()` — 栈导航
- `App::display` — Display 指针

### LVGL
- 所有 `lv_*` 函数（LVGL 9.5 API）
- `Display::lockGuard()` — 获取 LVGL 锁

### FreeRTOS
- `xTaskCreate` / `vTaskDelay` / `xQueueSend` 等
- `Task::addTask()` — 协程调度器

### 单例
- `VirtualGamepad::instance()` — 手柄输入
- `BleGamepad::instance()` — BLE 管理
- `BatteryManager::instance()` — 电池信息
- `Audio::instance()` — 音频播放

### 工具
- `cJSON_*` — JSON 解析
- `ESP_LOGI/D/W/E` — 日志输出
- `GUI::create*()` — UI 工厂方法

## ⚠ 关键注意事项

### 禁止文件级非 POD static

ELF loader 不执行 `.init_array`，文件级对象不会被构造：

```cpp
// ❌ 错误：文件级 static 对象
static SomeClass g_instance;  // 不会被构造！

// ✅ 正确：函数局部 static
static SomeClass& getInstance() {
    static SomeClass inst;  // 首次调用时构造
    return inst;
}
```

### FREERTOS_HZ 必须同步

插件的 `sdkconfig.defaults` 必须包含 `CONFIG_FREERTOS_HZ=1000`，与主固件一致。否则 `pdMS_TO_TICKS` 时序差 10 倍。

### std::string 跨边界

`std::string` 可以跨 .so 边界传递（同工具链），但不要返回 `std::vector` 等复杂容器。

### 符号缺失排查

如果插件加载失败报 `Can't find common`，说明主固件符号表缺少所需符号：

```bash
# 查看插件引用的未定义符号
nm -u build/<name>.so | c++filt

# 检查是否在符号表中
grep "缺失符号名" main/elfSdk/elf_symtab.c
```

### .so 体积优化

IDF 默认注入 `-ggdb` 调试信息，占 .so 体积 50-70%。`build_elf.sh` 会根据优化级别自动 strip：
- DEBUG/NONE → 保留调试信息
- SIZE/PERF → 剥离调试信息
