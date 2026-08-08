idf_component_register(SRCS "main.cpp"
                            "iic/iic.cpp"
                            "wifi/wifi.cpp" "wifi/slave.cpp" "wifi/mdns.cpp" "wifi/socketStream.cpp" "wifi/socketStreamWindow.cpp" "wifi/http.cpp" "websocket/wsClient.cpp"
                            "display/display.cpp" "display/ili9881c.cpp" "display/font.cpp"
                            "touch/touch.cpp" "touch/gt911Indev.cpp"
                            "audio/ES8311.cpp" "audio/Audio.cpp"
                            "storage/fat.cpp" "storage/mem.cpp" "storage/sd.cpp"

                            "task/task.cpp"
                            "gui/gui.cpp"
                            "utility/stringCompare.cpp"
                            "monitor/cpuMonitor.cpp"
                            "battery/batteryManager.cpp" "battery/batteryMonitor.cpp"
                            "power/powerManager.cpp"
                            "time/timeService.cpp"
                            "discovery/discovery.cpp"

                            "server/serverKernal.cpp"
                            "websocket/wsServer.cpp"
                            "screenStream/screenStream.cpp" "screenStream/wsStreamHandler.cpp"
                            "gamepad/bleGamepad.cpp" "gamepad/virtualGamepad.cpp"
                            "host/inputHost.cpp" "host/wsInputHandler.cpp" "host/hostManager.cpp" "host/wsConsoleHandler.cpp" "host/udpInput.cpp"
                            "virtualIndev/virtualIndev.cpp" "virtualIndev/wsTouchHandler.cpp"
                            "lua/luaRuntime.cpp" "lua/luaModules.cpp" "lua/bindings/luaDisplay.cpp" "lua/bindings/luaInput.cpp" "lua/bindings/luaAudio.cpp" "lua/bindings/luaThread.cpp" "lua/bindings/luaStorage.cpp" "lua/bindings/luaTouch.cpp" "app/luaGameApp/luaGameApp.cpp"

                            "elfSdk/elf_sdk.cpp" "elfSdk/elf_symtab.c"
                            "elfManager/elfManager.cpp"

                            "app/app.cpp" "app/appStack.cpp" "app/appStackManager.cpp"
                            "app/testApp/testApp.cpp"
                            "app/desktopApp/desktopApp.cpp"
                            "app/bleSettingsApp/bleSettingsApp.cpp"
                            "app/powerManagementApp/powerManagementApp.cpp"
                            "app/wifiSettingsApp/wifiSettingsApp.cpp"
                            "app/timeSettingsApp/timeSettingsApp.cpp"
                            "app/aiAgentApp/aiAgentOverlay.cpp" "app/aiAgentApp/pinyinDict.cpp"
                            "app/consoleManager/consoleManagerApp.cpp"
                            "app/luaManagerApp/luaManagerApp.cpp"
                            "app/marketApp/marketApp.cpp"

                       INCLUDE_DIRS "."
                       "lua"
                       "../managed_components/espressif__esp_lvgl_adapter/src/display"
                       "../managed_components/espressif__esp_lvgl_adapter/src/adapter"
                       "../managed_components/espressif__esp_lvgl_adapter/src/display/ports"
                       REQUIRES nvs_flash esp_wifi esp_event esp_netif lwip mdns lua
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_lvgl_adapter fatfs esp_driver_jpeg esp_http_server esp_websocket_client esp_codec_dev esp_audio_codec esp_audio_render gmf_core gmf_audio bt json esp_http_client elf_loader)

# Lua 需要 32-bit 模式（ESP32-P4 RISC-V 架构）
target_compile_definitions(${COMPONENT_LIB} PRIVATE LUA_32BITS=1)

# ── LVGL 构建选项覆盖 ────────────────────────────────────────
# LV_INV_BUF_SIZE：失效区域缓冲大小（lv_display_private.h 中 #ifndef 保护）。
# 编译期 -D 优先级高于头文件默认值，故用编译宏注入到 lvgl__lvgl 组件。
# PUBLIC 沿链接接口传播到 esp_lvgl_adapter / main，保证 lv_display_t 结构体布局
# （inv_areas[LV_INV_BUF_SIZE]）在所有 TU 一致，否则 ABI 不一致会内存踩踏。
# 与 esp_lvgl_adapter 注入 LV_CACHE_DEF_SIZE 的方式一致（官方姿势）。
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
if(TARGET ${lvgl_lib})
    target_compile_definitions(${lvgl_lib} PUBLIC LV_INV_BUF_SIZE=256)
endif()

# ── ELF 插件符号保活：--undefined 强制保留 ──────────────────
# 背景：符号表（elf_symtab.c）从主固件 .elf 提取，而 .elf 只含被引用的符号。
#       插件（.so）引用但主固件未引用的符号会被 --gc-sections 回收 → .elf 没有
#       → 无法导出 → dlopen "Can't find common"。
# 机制：--undefined=<sym> 是链接器原生强制：无条件保留符号（即使无人引用）。
#       ⚠ 必须用 INTERFACE 传播：main 是静态库（libmain.a），PRIVATE 链接选项
#         不参与最终 firmware 链接，会静默失效（曾踩坑）。
#       ⚠ 曾尝试 __attribute__((used, retain)) 数组保活，但 ESP-IDF 自定义链接
#         脚本不收集 retain 节（数组整个被 GC）——已废弃，统一用 --undefined。
# 维护：tools/check_elf_syms.sh 构建插件后自动对照 .elf 检查缺失符号；
#       缺哪个就往下面列表加一行。注意此列表只放"主固件完全不引用"的少数符号，
#       大部分缺失其实是白名单漏选（tools/gen_elf_syms.py 的 C_EXACT/LIBGCC_PREFIXES）。
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--undefined=lv_obj_set_style_transform_scale_x"
    "-Wl,--undefined=lv_obj_set_style_transform_scale_y"
    # wsServerUnregister：主固件只有 tetris 调用（tetrisNetDeinit 注销 /ws/tetris），
    # tetris 迁出 ELF 后被 GC 回收 → 保活供 .so 解析（Phase 3 联机游戏迁移）
    "-Wl,--undefined=_Z18wsServerUnregisterPKc"
    # __dso_handle：插件文件级非 POD static（带析构）引用，主固件自身由 libcxx（cxx_guards.cpp）
    # 定义；--undefined 保活防 GC（导出供 .so 解析，勿自定义——会与 libcxx 重定义）
    "-Wl,--undefined=__dso_handle"
    # ── Phase 3：tetris 迁出 ELF 后，以下符号唯一调用方消失 → 被 --gc-sections 回收 ──
    # 保活进 .elf 供插件解析（gen_elf_syms.py 白名单会自动选中：lv_ 前缀 / AudioSample、App 类）
    "-Wl,--undefined=lv_canvas_create"
    "-Wl,--undefined=lv_canvas_get_draw_buf"
    "-Wl,--undefined=lv_canvas_set_buffer"
    "-Wl,--undefined=lv_obj_set_style_margin_top"
    "-Wl,--undefined=lv_obj_set_style_margin_bottom"
    "-Wl,--undefined=_ZN11AudioSample4loadEPKc"
    "-Wl,--undefined=_ZN11AudioSample4playEb"
    "-Wl,--undefined=_ZN11AudioSample6unloadEv"
    "-Wl,--undefined=_ZN3App11replaceWithEPS_"
    # douDiZhu 迁出 ELF 后唯一调用方消失 → GC 回收（douDiZhuTable 用）→ 保活
    "-Wl,--undefined=lv_obj_set_style_text_line_space")
