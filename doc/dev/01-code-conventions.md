# 代码约定与规范

## 命名规范

| 类型 | 风格 | 示例 |
|------|------|------|
| 文件名 | camelCase | `display.hpp`, `serverKernal.cpp` |
| 类名 | PascalCase | `AppStackManager`, `LuaRuntime` |
| 方法/函数 | camelCase | `lockGuard()`, `getPercent()` |
| 成员变量 | camelCase（无统一前缀） | `m_display`, `m_mutex` |
| 常量 | camelCase | `constexpr char TAG[]` |
| 局部变量 | camelCase | `frameBufferCount` |
| 宏/枚举值 | PascalCase | `BTN_A`, `FontSize::Small` |

### 前缀约定

- `m_` 前缀：私有成员变量（`m_display`, `m_slots`）
- `s_` 前缀：静态变量（`s_fonts`, `s_encodeTask`）
- `k` 前缀：编译期常量（`kMaxSegments`, `kFadeLevels`）

## 文件组织

### 头文件

```cpp
#pragma once  // 替代 include guard

#include <标准库头文件>
#include <ESP-IDF 头文件>
#include "项目内头文件"
```

### 源文件

```cpp
#include "对应头文件.h"
#include "其他依赖.h"

static constexpr char TAG[] = "模块名";  // ESP_LOG TAG

// 静态辅助函数
static void helperFunc() { }

// 类方法实现
void ClassName::method() { }
```

## 日志规范

**所有日志使用 ESP_LOGx 宏**，禁止 `printf` 输出日志。

```cpp
ESP_LOGE(TAG, "错误: %s", errStr);    // ERROR
ESP_LOGW(TAG, "警告: 超时");           // WARN
ESP_LOGI(TAG, "初始化完成");           // INFO
ESP_LOGD(TAG, "收到 %d 字节", len);    // DEBUG
ESP_LOGV(TAG, "详细: %02x", buf[0]);   // VERBOSE
```

### 级别使用指南

| 级别 | 用途 | 示例 |
|------|------|------|
| ERROR | 初始化失败、不可恢复错误 | `ESP_LOGE(TAG, "I2C 探测失败")` |
| WARN | 可恢复异常、降级处理 | `ESP_LOGW(TAG, "字体加载失败，使用默认")` |
| INFO | 生命周期事件、状态变更 | `ESP_LOGI(TAG, "WiFi 已连接: %s", ssid)` |
| DEBUG | 请求级日志、频繁调用 | `ESP_LOGD(TAG, "WS 帧: %zu bytes", len)` |
| VERBOSE | 调试细节、通常关闭 | `ESP_LOGV(TAG, "IRQ: reg=0x%02x", reg)` |

## 错误处理

### 关键初始化

```cpp
ESP_ERROR_CHECK(esp_event_loop_create_default());
```

### 常规错误

```cpp
if (!mountFlash()) {
    ESP_LOGE(TAG, "FAT 挂载失败");
    return false;
}
```

### 无 C++ 异常

项目禁用 C++ 异常（`-fno-exceptions`），所有错误通过返回值或错误码处理。

## 内存管理

- **PSRAM 是主堆**：`CONFIG_SPIRAM_USE_MALLOC=y`，所有 `malloc` 优先分配到 PSRAM
- **内部 RAM 保留**：关键路径（OTA 写入、ELF 加载栈、中断处理）必须使用内部 RAM
- **PSRAM 限制**：DMA 操作、flash 缓存暂停期间不能访问 PSRAM

## 并发模型

### FreeRTOS 双核

- Core 0：BLE、WiFi
- Core 1：LVGL、应用逻辑

### 锁类型

| 锁 | 类型 | 用途 |
|----|------|------|
| `Mutex` | 非递归互斥锁 | BleGamepad 等内部保护 |
| `RecursiveMutex` | 递归互斥锁 | AppStackManager |
| `Display::LockGuard` | LVGL 递归锁 | LVGL 线程安全 |

### 锁序规则

- `dispatchLock → LVGL`：VirtualGamepad 分发锁在前
- LVGL 事件回调中不可执行栈操作（用 `Task::addTask` 延后）

## App 生命周期

```
构造 → init() → onForeground() ↔ onBackground() → deinit() → 析构
```

- `init()`：标记 `running=true`, `deletable=false`
- `deinit()`：标记 `running=false`, `deletable=true`
- 异步删除：`scheduleDeletion()` → 独立 Task 中 `deinit()` → 等 `deletable` → `delete`

## 去重模式

AppStack 操作需手动防重入：

```cpp
TickType_t m_nextActionTime = 0;
static constexpr TickType_t ACTION_COOLDOWN_MS = 500;

void onSomeAction() {
    if (xTaskGetTickCount() < m_nextActionTime) return;
    m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN_MS;
    // ... 执行操作
}
```

## 提交格式

```
<type>: <中文描述>
```

类型：
- `feat`: 新功能
- `pref`: 性能优化
- `refactor`: 重构
- `style`: 代码风格
- `fix`: Bug 修复
- `docs`: 文档
- `chore`: 构建/工具

## 注释语言

- **代码注释**：中文
- **变量名/方法名**：英文
- **文档**：中文

## 文件组织

### 头文件

```cpp
#pragma once  // 替代 include guard

#include <标准库头文件>
#include <ESP-IDF 头文件>
#include "项目内头文件"
```

### 源文件

```cpp
#include "对应头文件.h"
#include "其他依赖.h"

static constexpr char TAG[] = "模块名";  // ESP_LOG TAG

// 静态辅助函数
static void helperFunc() { }

// 类方法实现
void ClassName::method() { }
```

## 日志规范

**所有日志使用 ESP_LOGx 宏**，禁止 `printf` 输出日志。

```cpp
ESP_LOGE(TAG, "错误: %s", errStr);    // ERROR
ESP_LOGW(TAG, "警告: 超时");           // WARN
ESP_LOGI(TAG, "初始化完成");           // INFO
ESP_LOGD(TAG, "收到 %d 字节", len);    // DEBUG
ESP_LOGV(TAG, "详细: %02x", buf[0]);   // VERBOSE
```

### 级别使用指南

| 级别 | 用途 | 示例 |
|------|------|------|
| ERROR | 初始化失败、不可恢复错误 | `ESP_LOGE(TAG, "I2C 探测失败")` |
| WARN | 可恢复异常、降级处理 | `ESP_LOGW(TAG, "字体加载失败，使用默认")` |
| INFO | 生命周期事件、状态变更 | `ESP_LOGI(TAG, "WiFi 已连接: %s", ssid)` |
| DEBUG | 请求级日志、频繁调用 | `ESP_LOGD(TAG, "WS 帧: %zu bytes", len)` |
| VERBOSE | 调试细节、通常关闭 | `ESP_LOGV(TAG, "IRQ: reg=0x%02x", reg)` |

## 错误处理

### 关键初始化

```cpp
ESP_ERROR_CHECK(esp_event_loop_create_default());
```

### 常规错误

```cpp
if (!mountFlash()) {
    ESP_LOGE(TAG, "FAT 挂载失败");
    return false;
}
```

### 无 C++ 异常

项目禁用 C++ 异常（`-fno-exceptions`），所有错误通过返回值或错误码处理。

## 内存管理

- **PSRAM 是主堆**：`CONFIG_SPIRAM_USE_MALLOC=y`，所有 `malloc` 优先分配到 PSRAM
- **内部 RAM 保留**：关键路径（OTA 写入、ELF 加载栈、中断处理）必须使用内部 RAM
- **PSRAM 限制**：DMA 操作、flash 缓存暂停期间不能访问 PSRAM

## 并发模型

### FreeRTOS 双核

- Core 0：BLE、WiFi
- Core 1：LVGL、应用逻辑

### 锁类型

| 锁 | 类型 | 用途 |
|----|------|------|
| `Mutex` | 非递归互斥锁 | BleGamepad 等内部保护 |
| `RecursiveMutex` | 递归互斥锁 | AppStackManager |
| `Display::LockGuard` | LVGL 递归锁 | LVGL 线程安全 |

### 锁序规则

- `dispatchLock → LVGL`：VirtualGamepad 分发锁在前
- LVGL 事件回调中不可执行栈操作（用 `Task::addTask` 延后）

## App 生命周期

```
构造 → init() → onForeground() ↔ onBackground() → deinit() → 析构
```

- `init()`：标记 `running=true`, `deletable=false`
- `deinit()`：标记 `running=false`, `deletable=true`
- 异步删除：`scheduleDeletion()` → 独立 Task 中 `deinit()` → 等 `deletable` → `delete`

## 去重模式

AppStack 操作需手动防重入：

```cpp
TickType_t m_nextActionTime = 0;
static constexpr TickType_t ACTION_COOLDOWN_MS = 500;

void onSomeAction() {
    if (xTaskGetTickCount() < m_nextActionTime) return;
    m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN_MS;
    // ... 执行操作
}
```

## 提交格式

```
<type>: <中文描述>
```

类型：
- `feat`: 新功能
- `pref`: 性能优化
- `refactor`: 重构
- `style`: 代码风格
- `fix`: Bug 修复
- `docs`: 文档
- `chore`: 构建/工具

## 注释语言

- **代码注释**：中文
- **变量名/方法名**：英文
- **文档**：中文
