# patches — managed_components 补丁集

本目录存放对 `managed_components/` 中组件源码的补丁，使用跨平台 Python 脚本管理。

## 快速使用

```bash
python patches/apply.py            # 应用所有补丁（git apply，无 fuzz）
python patches/apply.py --check    # 干跑：报告每个补丁状态，不改动文件
python patches/apply.py --revert   # 还原所有补丁
```

前置要求：Python 3（标准库）+ **git**（ESP-IDF 必装，跨平台无额外依赖）。
仅适用于 Linux / macOS / Windows 三平台一致的 `git apply` 语义。

> ⚠ `git apply` 不支持 fuzz：组件版本升级后若补丁上下文漂移会直接报错。
> 版本更新后需重新生成/修正补丁。

## 补丁清单

| # | 组件 | 文件 | 说明 |
|---|------|------|------|
| `0001-esp_lvgl_adapter-frame-ready-callback.patch` | `espressif__esp_lvgl_adapter` | `display_bridge.h` / `lvgl_bridge_v9.c` / `display_manager.h/.c` | Bridge 帧完成回调 `on_frame_ready` → `display_manager_set_frame_ready_callback()`，供 ScreenStream 零拷贝采集（必需） |
| `0002-lvgl__lvgl-ime-pinyin-fixes.patch` | `lvgl__lvgl` | `src/widgets/ime/lv_ime_pinyin.c` | 输入法 IME 修复合集：① 移除 i/u/v 开头过滤（五笔键区）② `count` uint8→uint16（大词库不截断）③ 候选空槽 HIDDEN + 选中重置 ④ 翻页焦点保持 ⑤ 清除残留 PRESSED/CHECKED（必需） |
| `0003-esp_lv_decoder-disable-png.patch` | `espressif__esp_lv_decoder` | `src/lvgl9/esp_lv_decoder.c` / `include/esp_lv_decoder_config.h` | PNG 支持用 `#if ESP_LV_ENABLE_PNG` 包裹；主固件注入 `-DESP_LV_ENABLE_PNG=0` 裁掉 libpng+zlib（必需） |

> 说明：PPA 卡死修复为 `esp_lvgl_adapter` 组件随包附带的官方补丁（非本目录补丁），见组件 README。

## 新增补丁约定

1. 命名：`NNNN-<组件名>-<描述>.patch`（组件名 = `managed_components/` 下的目录名，如 `lvgl__lvgl`、`espressif__esp_lv_decoder`）。
2. 路径以组件目录为根（`-p1`），即补丁内路径形如 `a/src/...` / `b/src/...`。
3. 在 `apply.py` 的 `MANIFEST` 中登记（组件目录）。
4. 生成后先用 `python patches/apply.py --check` 验证可应用。

## 组件版本基线（补丁生成时）

| 组件 | 版本 |
|------|------|
| lvgl/lvgl | 9.5.0 |
| espressif/esp_lvgl_adapter | 0.4.3~1 |
| espressif/esp_lv_decoder | 0.4.3 |
| espressif/esp_websocket_client | 1.7.0 |
| idf | 5.5.2 |
