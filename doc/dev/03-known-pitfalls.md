# 已知坑与调试指南

## P4 内部 RAM 三原则

ESP32-P4 的 PSRAM 依赖 cache 访问，以下场景**必须使用内部 RAM**：

1. **OTA API 调用栈**：`esp_ota_*` 内部会禁用 cache → PSRAM 栈 = assert 崩溃
2. **OTA 写入缓冲**：`esp_ota_write` 源缓冲在 PSRAM = 非法访问
3. **ELF 加载任务栈**：`esp_elf_arch_flush` 调 `spi_flash_disable_interrupts_caches_and_other_cpu` → 栈必须在内部 RAM

```cpp
// ❌ 错误：PSRAM 栈执行 OTA
xTaskCreate(otaTask, "ota", 4096, ...);  // 默认 PSRAM 栈

// ✅ 正确：内部 RAM 静态栈
static StackType_t s_otaStack[8192];
static StaticTask_t s_otaTcb;
xTaskCreateStatic(otaTask, "ota", 8192, ..., s_otaStack, &s_otaTcb);
```

## Mutex 拷贝陷阱

`Mutex` / `RecursiveMutex` 的拷贝/移动已 `= delete`，但编译器可能静默生成：

```cpp
// ❌ 危险：按值传递 = 拷贝锁句柄 + 临时对象析构删除句柄
void foo(Mutex m) { m.lock(); }  // 悬空句柄！

// ✅ 正确：按引用传递
void foo(const Mutex& m) { m.lock(); }
```

含锁成员的对象**必须用单例/常驻存储**，不可 `std::move` 赋值。

## LVGL 锁规则

### 可重入锁

`Display::LockGuard` 使用递归互斥锁，同一 task 可多次加锁：

```cpp
// ✅ 同一 task 内嵌套
if (auto guard = display->lockGuard()) {
    if (auto inner = display->lockGuard()) {  // 不死锁
        // ...
    }
}
```

### 事件回调限制

LVGL 事件回调（`lv_event_cb`）中**不可执行栈操作**：

```cpp
// ❌ 错误：直接 push/pop
static void on_click(lv_event_t* e) {
    auto* app = static_cast<App*>(lv_event_get_user_data(e));
    app->pushApp(new NextApp(app->display));  // LVGL 异常！
}

// ✅ 正确：延后执行
static void on_click(lv_event_t* e) {
    auto* self = static_cast<MyApp*>(lv_event_get_user_data(e));
    Task::addTask([](void* p) -> TickType_t {
        static_cast<MyApp*>(p)->pushApp(new NextApp(p->display));
        return Task::infinityTime;
    }, "deferred", self, 0, Task::Affinity::None);
}
```

## VirtualGamepad UAF 防护

删除 App 前必须调用 `waitDispatchIdle()`，确保 dispatchTask 当前帧 handler 已返回：

```cpp
// ❌ 错误：dispatchTask 可能在已删 App 上调用虚函数
delete app;  // UAF!

// ✅ 正确：等待分发结束
VirtualGamepad::instance().waitDispatchIdle();
delete app;
```

## PSRAM DMA 缓存一致性

DMA 操作（JPEG 编码、PPA 渲染）读写 PSRAM 时需注意缓存一致性：

- JPEG 缓冲必须用 `jpeg_alloc_decoder_mem` 分配（DMA 对齐）
- PPA 操作使用 `MALLOC_CAP_DMA` 缓冲
- `heap_caps_malloc` 普通分配可能不对齐

## 字体渲染问题

### FreeType IMAGE 模式

`lv_font_get_glyph_bitmap` 返回 `lv_draw_buf_t*`（非像素数组），位图 stride 对齐：

```cpp
// ❌ 错误：直接 memcpy
memcpy(dest, bitmap, width * height);

// ✅ 正确：按 stride 逐行复制
for (int y = 0; y < height; y++) {
    memcpy(dest + y * destStride,
           src + y * srcStride,
           width * bytesPerPixel);
}
```

### 字体回退链

NotoSC 缺少符号字体的字形时会显示方块，需要设置回退：

```cpp
const_cast<lv_font_t*>(FontLoader::getDefault(FontSize::Small))->fallback = lv_font_get_default();
```

## HTTP Body 残留

`ISocketStream::read()` 非阻塞（`MSG_DONTWAIT`），单次调用可能读不满 body。残留字节会被同一 TCP 连接的下一个请求误当请求行。

```cpp
// ✅ 所有 body 读取必须循环读完
void readBodyFully(ISocketStream& stream) {
    while (stream.isGood()) {
        stream.readByte();
        vTaskDelay(1);
    }
}
```

## ELF loader 不执行 .init_array

文件级 static 对象不会被构造：

```cpp
// ❌ 错误
static SomeClass g_instance;  // 不会被构造！

// ✅ 正确：函数局部 static
static SomeClass& getInstance() {
    static SomeClass inst;
    return inst;
}
```

## FREERTOS_HZ 同步

插件的 `sdkconfig.defaults` 必须包含 `CONFIG_FREERTOS_HZ=1000`。不同步会导致：
- `pdMS_TO_TICKS` 时序差 10 倍
- `vTaskDelay` 行为异常
- 游戏循环帧率错误

## LVGL 失效缓冲溢出

大量 LVGL 对象同时更新时，`LV_INV_BUF_SIZE`（默认 256）可能溢出 → 整屏重绘。

解决方案：
- 使用 canvas 化渲染（FruitNinja、Tetris）
- 脏区集合失效（精确不合并）
- 减少每帧更新对象数

## 调试技巧

### 远程日志

封壳设备无串口，通过 WebSocket 实时日志调试：

```bash
# 浏览器打开诊断页
http://<hostname>.local/server/diag/

# 或 curl 获取日志
curl http://<hostname>.local/api/log?tail=200
```

### 崩溃转储

```bash
# 下载 coredump
curl -o core.elf http://<hostname>.local/api/coredump

# 解码
idf.py coredump-info -c core.elf build/The-Defect.elf
```

### 内存监控

SystemMonitor 每秒输出：
```
[SysMon] CPU: 23%, C0: 21%, C1: 25%
[SysMon] RAM:248KB/415KB(59%) PSRAM:7.3MB/28.1MB(26%) FPS:60
```

### PSRAM 泄漏检测

```cpp
// 测量 PSRAM 剩余
size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
ESP_LOGI(TAG, "PSRAM free: %zu bytes", free);
```

### 符号缺失排查

```bash
# 查看插件引用的未定义符号
nm -u build/<name>.so | c++filt

# 检查是否在符号表中
grep "缺失符号名" main/elfSdk/elf_symtab.c

# 重新生成符号表
python tools/gen_elf_syms.py
```

### HTTP body 残留诊断

如果收到诡异的 POST 请求（如 `path="name":`），可能是上一个请求 body 残留：
- 检查所有 body 读取是否循环读完
- 使用 `readBodyFully()` 辅助函数
