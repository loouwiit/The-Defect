cmake_minimum_required(VERSION 3.5)

# 复用主工程已下载的组件（必须在 project() 之前设置）
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__cmake_utilities
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/lvgl__lvgl
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_lvgl_adapter
    # tetris 使用 Audio（tetrisApp.hpp → audio/Audio.hpp → esp_audio_render/esp_audio_dec/esp_gmf_pool）
    # 复用主工程 managed_components 的音频组件——.so 编译只需其头文件（链接仅收 __idf_main，不包含音频代码）
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_audio_render
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__gmf_audio
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_audio_codec
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_audio_effects
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__gmf_core
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__gmf_fft
    # esp_websocket_client（tetris_net.cpp 客户端）：主固件用 managed 版本 ^1.7.0，
    # 插件必须复用同一份头文件（API 布局一致，符号运行时解析到主固件）
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_websocket_client
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tetris)

# ── C++ .so 构建 ─────────────────────────────────────────────
# 同 snake：tetris 源作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖），
# 这里仅把组件库（libmain.a，只含 tetris 的 .o）强制全量链接为 .so。
# 链接 flags：-nostdlib -static-libgcc；libstdc++ 不链接 → 运行时符号
# （operator new / wsServer* / lwip_* / httpd_ws_* 等）保持 UND，运行时解析到主固件注册表。
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
