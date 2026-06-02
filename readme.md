# 目标
* P4驱动的多人游戏机
* C6驱动的手柄

# 功能

* 打游戏
* 多人联机
* * 创建加入
* * 碰一碰联机（可选）
* HID设备（可选）

## 打游戏
* 俄罗斯方块：手柄输入
* 贪吃蛇：手柄输入
* 水果忍者：触屏输入
* 中国象棋：手柄、触屏输入
* 斗地主/麻将（可选）：手柄、触屏输入
* 平衡球（可选）：重力

## 多人联机
实现：将输入抽象。一个输入对于一个inputer。

同步：游戏行为由事件驱动，仅由主机发送事件。或者每次发送全部游戏帧。

写app代码的时候一定一定要遵循抽象设计。

## HID设备（可选）

## 小功能
* 电池电量
* 手柄拆卸
* 时间
* 投屏（没必要）

# 硬件选型

|	硬件	|	单个成本	|	数量	|
|:-:|:-:|:-:|
|	P4C6	|	60	|	1	|
|	C6	|	20	|	2	|
|	720p屏幕	|	100	|	1	|
|	碰一碰（有待考虑）	|	60	|	1	|
|	电池充电模块	|	2	|	3	|
|	电池放电模块	|	8	|	3	|
|	电池包	|	10	|	3	|
|	按钮	|	2.5	|	9	|
|	遥杆	|	2	|	2	|
|	外壳	|	？？？	|	3	|
|	扬声器	|	20	|	3	|

## 屏幕
* 接口：MIPI 2line（ESP）
* 分辨率：720p
* 尺寸：6寸

# 通信架构

|通信设备|通信设备|协议|
|:-:|:-:|:-:|
|p4|c6|ESP HOST|
|p4|c6|滑轨内UART|
|c6|按钮|GPIO|
|c6|摇杆|ADC|
|p4|p4|esp-now|
|p4|电源|ADC/IIC|
|p4|屏幕|2line MIPI|
|p4|扬声器|IIS|

# 软件架构

## 要求
支持触摸（clickAble，内部有三个指针）

支持方向键（做绑定，focusAble，内部有四个指针）

支持返回键

支持音效播放

# 屏幕串流架构

## 概述

MJPEG 屏幕串流，通过 HTTP 实时投屏。

**模块路径**: `main/screenStream/`

**核心文件**:
- `screenStream.hpp` — `ScreenStream` 单例类
- `screenStream.cpp` — 回调 + JPEG 编码实现

## 设计原理

### 问题

LVGL 的 `lv_snapshot_take_to_draw_buf()` 会**完整重绘整个 UI 对象树**到新的 draw buffer。对于 720×1280 分辨率，每次截图都要遍历所有控件并重新渲染，开销极大，不适合高帧率串流。

### 方案

`esp_lvgl_adapter` 的 bridge 层内部维护了累积的完整帧缓冲（`disp_fb` / `draw_fb`），可直接读取。

我们在 bridge 抽象接口中新增了 `on_frame_ready` 回调，每帧 flush 完成后触发，提供 front buffer 指针。`ScreenStream` 在其回调中缓存指针，`captureJpeg()` 直接送硬件 JPEG 编码器。

### 数据流

```
  LVGL 渲染
     ↓
  bridge flush (逐个 dirty tile)
     ↓
  copy_unrendered → cache sync → blit to panel → swap disp_fb/draw_fb
     ↓
  on_frame_ready(disp, disp_fb, size, ctx)    ← 每帧 1 次
     ↓
  ScreenStream::frameReadyCallback: 缓存 fb 指针
     ↓ (定时器或 HTTP 请求触发)
  ScreenStream::captureJpeg:
    → jpeg_enc_process(hw, disp_fb, size, out_buf, &out_sz)
    → 返回 JPEG 数据
```

## Bridge 补丁

`esp_lvgl_adapter` 的 4 个文件被修改，以 patch 形式在 `patches/` 中管理：

| 文件 | 改动 |
|------|------|
| `src/display/ports/display_bridge.h` | 抽象接口末尾加 `on_frame_ready` + `frame_ready_user_ctx` |
| `src/display/bridge/v9/lvgl_bridge_v9.c` | 7 个 flush 函数的帧完成点插入回调调用 |
| `src/display/display_manager.h` | 新增 `display_manager_set_frame_ready_callback()` 声明 |
| `src/display/display_manager.c` | 实现注册函数 |

**使用**:
```bash
bash patches/apply.sh           # 打补丁
bash patches/apply.sh --revert  # 还原
bash patches/apply.sh --check   # 检查状态
```

⚠ `idf.py reconfigure` 后 `managed_components` 会重置，需要重新 `bash patches/apply.sh`。

## 性能影响

| 场景 | 开销 |
|------|------|
| 串流关闭 | bridge 中 1 次 null 指针检查/帧（≈ 2-3 周期） |
| 串流开启 | 回调中 2 次指针赋值 + JPEG 编码时间（硬件加速，与 UI 复杂度无关） |
| 对比 `lv_snapshot` | 消除全部重新渲染开销，帧率提升显著 |

## mDNS 服务发现

基于 ESP-IDF mdns 组件，实现 mDNS 服务发现。

**模块路径**: `main/wifi/mdns.cpp`

**核心文件**:
- `mdns.hpp` — 接口声明
- `mdns.cpp` — 实现

### 功能

设备在网络上广播主机名和服务，支持 mdns 客户端通过 `esp32p4.local` 发现设备，无需记忆 IP 地址。

### 使用方法

```cpp
mdnsInit();                                          // 1. 初始化
mdnsStart("esp32p4", "ESP32P4 Game Console");       // 2. 设置主机名和实例名
mdnsServiceAdd("ESP32P4 HTTP", "_http", "_tcp", 80); // 3. 添加 HTTP 服务
mdnsServiceAdd("ESP32P4 WS", "_ws", "_tcp", 8080);   // 4. 添加 WebSocket 服务
```

### 可发现的服务

| 实例名 | 服务类型 | 协议 | 端口 |
|--------|----------|------|------|
| ESP32P4 HTTP | _http | _tcp | 80 |
| ESP32P4 WS | _ws | _tcp | 8080 |

## FontLoader — 字体加载

**模块路径**: `main/display/font.cpp`

### 概述

`FontLoader` 是一个无状态的纯工具类，负责从 VFS 路径加载 FreeType 字体，并提供全局默认字体。

### 设计

```cpp
class FontLoader {
public:
    // 加载字体文件，返回 lv_font_t*
    // 生命周期由 esp_lv_adapter_deinit() 统一管理
    static const lv_font_t* load(const char* vfsPath);

    // 全局默认字体（供所有对象使用）
    static const lv_font_t* getDefault();
    static void setDefault(const lv_font_t* font);

private:
    FontLoader() = delete;
    ~FontLoader() = delete;

    static const lv_font_t* s_defaultFont;
};
```

### VFS 驱动器 'F'

FontLoader 在内部注册 LVGL VFS 驱动器 `'F'`，将 `"F:..."` 路径映射到 `/root/`：

```
FreeType "F:system/NotoSC.ttf"
    → LVGL VFS driver 'F'
    → convert_path: 去掉 "F:" 前缀，拼接到 "/root/"
    → fopen("/root/system/NotoSC.ttf")
    → VFS → FAT flash
```

### 使用方式

```cpp
// main.cpp — 初始化时加载并设为默认
FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf"));

// App 构造函数 — 自动继承字体到所有子对象
lv_obj_set_style_text_font(screen, FontLoader::getDefault(), 0);

// desktopApp — 直接使用默认字体，无需每次指定路径
lv_obj_set_style_text_font(label, FontLoader::getDefault(), 0);
```

### 关键特性

- **无状态**: FontLoader 不存储字体实例，每次 load() 返回新句柄
- **全局默认字体**: 通过 `setDefault()` / `getDefault()` 提供共享字体
- **样式继承**: App 基类在 screen 上设置默认字体，所有子对象自动继承
- **生命周期**: 字体由 `esp_lv_adapter_deinit()` 统一释放，无法单独 deinit

## 资源文件

`reserces/` 目录下的文件会被上传到 ESP32-P4 内置 FLASH 的 FAT 分区，挂载路径为 `/root/`。

**编译烧录后**，需要通过 HTTP 上传资源文件到设备：

1. 访问 `http://esp32p4.local/file` 或 `http://<设备IP>/file`
2. 进入对应目录，上传以下文件：

### 目录结构

| 路径 | 说明 |
|------|------|
| `/root/system/NotoSC.ttf` | 思源黑体字体文件（FontLoader 所需） |
| `/root/server/` | Web 服务器静态资源目录 |

### 资源说明

| 资源 | 必需 | 说明 |
|------|------|------|
| `system/NotoSC.ttf` | **必需** | 思源黑体 Regular，FontLoader 按字号加载 |
| `server/` | 可选 | 网页静态资源，用于 HTTP 服务器 |

### 上传示例

```bash
# 通过 curl 上传字体文件
curl -X PUT -T system/NotoSC.ttf http://esp32p4.local/file/system/NotoSC.ttf
```

⚠ 上传资源后需要重启设备以重新加载字体。
