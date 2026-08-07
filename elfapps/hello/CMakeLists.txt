cmake_minimum_required(VERSION 3.5)

# 复用主工程已下载的 elf_loader 组件（含 cmake_utilities 依赖），
# 避免独立工程依赖组件管理器/网络重复下载。必须在 project() 之前设置。
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__elf_loader
    ${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__cmake_utilities
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello)

include(elf_loader)
project_so(hello)
