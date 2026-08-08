cmake_minimum_required(VERSION 3.5)

# 复用主工程已下载的组件（必须在 project() 之前设置）
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__cmake_utilities
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/lvgl__lvgl
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_lvgl_adapter
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smoke)

# ── C++ .so 构建 ─────────────────────────────────────────────
# 方案：smoke.cpp 作为正常组件（main/CMakeLists.txt）编译为 PIC 目标文件，
# IDF 组件系统全自动解析 REQUIRES 传递依赖（lvgl/esp_lvgl_adapter/esp_lcd 等），
# 这里仅把组件库（libmain.a，只含 smoke.cpp.o）强制全量链接为 .so。
# 链接 flags：-nostdlib -static-libgcc；libstdc++ 不链接 → 运行时符号
# （operator new 等）保持 UND，运行时解析到主固件注册表。
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
