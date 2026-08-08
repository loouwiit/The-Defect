cmake_minimum_required(VERSION 3.5)

# 复用主工程已下载的组件（必须在 project() 之前设置）
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__cmake_utilities
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/lvgl__lvgl
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_lvgl_adapter
    # fruitNinja 使用 Audio（fruitNinjaRoom.hpp → audio/Audio.hpp → esp_audio_render/esp_audio_dec/esp_gmf_pool）
    # 复用主工程 managed_components 的音频组件——.so 编译只需其头文件（链接仅收 __idf_main，不包含音频代码）
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_audio_render
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__gmf_audio
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_audio_codec
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_audio_effects
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__gmf_core
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__gmf_fft
    # 触摸 ABI 契约（fruitNinjaApp.hpp → touch/gt911Indev.hpp → touch.hpp → esp_lcd_touch_gt911.h）：
    # 主固件与插件必须编译同一份头文件（类型布局一致，否则 Touch 跨边界错位），
    # 故纳入 ELF 接口面——依赖链均为 IDF 自带组件（esp_lcd/driver），无第三方传递
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_lcd_touch
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_lcd_touch_gt911
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fruitNinja)

# ── C++ .so 构建 ─────────────────────────────────────────────
# 同 snake：fruitNinja 源作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖），
# 这里仅把组件库（libmain.a，只含 fruitNinja 的 .o）强制全量链接为 .so。
# 链接 flags：-nostdlib -static-libgcc；libstdc++ 不链接 → 运行时符号
# （operator new / Audio::play / GUI:: / FontLoader 等）保持 UND，运行时解析到主固件注册表。
if(CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT)
    set(so_link_flags -shared
                      -fPIC
                      -static-libgcc
                      -nostdlib
                      -nostartfiles
                      -fdata-sections
                      -ffunction-sections
                      -Wl,--gc-sections
                      -fvisibility=hidden)

    set(so_output "${CMAKE_PROJECT_NAME}.so")

    add_custom_command(
        OUTPUT "${so_output}"
        COMMAND ${CMAKE_CXX_COMPILER} ${so_link_flags}
                -Wl,--whole-archive "$<TARGET_FILE:__idf_main>" -Wl,--no-whole-archive
                -o "${so_output}"
        DEPENDS __idf_main
        COMMENT "Link ${so_output} from __idf_main (PIC objects)")

    add_custom_target(so ALL DEPENDS "${so_output}")
endif()
