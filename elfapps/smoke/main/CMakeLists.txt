# smoke 插件组件：作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖，
# 含 esp_lcd 等，无需手拼 include）；根 CMakeLists 再把 PIC 目标文件链接为 .so。
idf_component_register(SRCS "smoke.cpp"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "."
                                         "../../../main"
                                         "../../../main/elfSdk"
                       REQUIRES lvgl esp_lvgl_adapter)
# .so 需要 PIC；关闭异常/RTTI 与主固件一致（插件不含这两者）
target_compile_options(${COMPONENT_LIB} PRIVATE -fPIC -fno-exceptions -fno-rtti)
