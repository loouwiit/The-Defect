# douDiZhu 插件组件：作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖）。
# PRIV_INCLUDE_DIRS 指向主工程头文件根（app.hpp/gui/display 等经此解析）。
# json：cJSON（IDF 内置）；lwip：lwip/sockets.h + lwip/netdb.h（IDF 内置）

idf_component_register(SRCS "douDiZhuPlugin.cpp"
                            "dsoHandle.c"
                            "app/douDiZhu/douDiZhuLogic.cpp"
                            "app/douDiZhu/douDiZhuAI.cpp"
                            "app/douDiZhu/douDiZhuTable.cpp"
                            "app/douDiZhu/douDiZhuApp.cpp"
                            "app/douDiZhu/douDiZhuRemoteApp.cpp"
                            "app/douDiZhu/douDiZhuRoom.cpp"
                            "app/douDiZhu/net/douDiZhuRemoteClient.cpp"
                            "app/douDiZhu/net/douDiZhuTcpHost.cpp"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "."
                                         "../../../main"
                                         "../../../main/elfSdk"
                       REQUIRES lvgl esp_lvgl_adapter fatfs
                                json lwip esp_audio_render gmf_audio esp_audio_codec esp_audio_effects gmf_core gmf_fft)
# .so 需要 PIC；关闭异常/RTTI 与主固件一致（插件不含这两者）
target_compile_options(${COMPONENT_LIB} PRIVATE -fPIC -fno-exceptions -fno-rtti)
