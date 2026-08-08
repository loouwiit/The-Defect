# chineseChess 插件组件：作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖）。
# 无音频/触摸/网络依赖；逻辑层纯 C++（std:: + esp_random + esp_timer_get_time，
# 符号由 gen_elf_syms.py 白名单导出）。PRIV_INCLUDE_DIRS 指向主工程头文件根。
# esp_timer：chessAI.hpp include esp_timer.h（AI 搜索计时）。
idf_component_register(SRCS "chineseChessPlugin.cpp"
                            "chineseChessRoom.cpp"
                            "chineseChessApp.cpp"
                            "chessLogic.cpp"
                            "chessAI.cpp"
                            "openingBook.cpp"
                            "transpositionTable.cpp"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "."
                                         "../../../main"
                                         "../../../main/elfSdk"
                       REQUIRES lvgl esp_lvgl_adapter esp_timer)
# .so 需要 PIC；关闭异常/RTTI 与主固件一致（插件不含这两者）
target_compile_options(${COMPONENT_LIB} PRIVATE -fPIC -fno-exceptions -fno-rtti)
