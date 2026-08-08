# fruitNinja 插件组件：作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖）。
# PRIV_INCLUDE_DIRS 指向主工程头文件根（app.hpp/audio/Audio.hpp/gui 等经此解析）；
# REQUIRES 的音频组件仅提供头文件（.so 链接不含其代码）。
# esp_driver_jpeg：fruitNinjaRenderer 用 driver/jpeg_decode.h 硬件解码水果图（IDF 自带组件）。
# esp_lcd_touch / esp_lcd_touch_gt911：触摸 ABI 契约（fruitNinjaApp.hpp → gt911Indev.hpp → touch.hpp），
#   主固件与插件必须编译同一份头文件，纳入 ELF 接口面。
# esp_timer：fruitNinjaApp.cpp include esp_timer.h（esp_timer_get_time 计时）。
idf_component_register(SRCS "fruitNinjaPlugin.cpp"
                            "fruitNinjaRoom.cpp"
                            "fruitNinjaApp.cpp"
                            "fruitNinjaLogic.cpp"
                            "fruitNinjaRenderer.cpp"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "."
                                         "../../../main"
                                         "../../../main/elfSdk"
                       REQUIRES lvgl esp_lvgl_adapter fatfs
                                esp_driver_jpeg esp_timer
                                esp_lcd_touch esp_lcd_touch_gt911
                                esp_audio_render gmf_audio esp_audio_codec esp_audio_effects gmf_core gmf_fft)
# .so 需要 PIC；关闭异常/RTTI 与主固件一致（插件不含这两者）
target_compile_options(${COMPONENT_LIB} PRIVATE -fPIC -fno-exceptions -fno-rtti)
