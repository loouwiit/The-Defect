# tetris 插件组件：作为正常组件编译（IDF 组件系统自动解析 REQUIRES 传递依赖）。
# PRIV_INCLUDE_DIRS 指向主工程头文件根（app.hpp/audio/Audio.hpp/gui 等经此解析）；
# REQUIRES 的音频组件仅提供头文件（.so 链接不含其代码）。
# esp_http_server：wsServer.hpp → esp_http_server.h（IDF 内置）
# esp_websocket_client：tetris_net.cpp 客户端（managed 版本，见根 CMakeLists）
# json：cJSON（IDF 内置）；lwip：lwip/sockets.h + lwip/netdb.h（IDF 内置）
idf_component_register(SRCS "tetrisPlugin.cpp"
                            "dsoHandle.c"
                            "app/tetris/gameLogic/tetris_client.cpp"
                            "app/tetris/gameLogic/player_state.cpp"
                            "app/tetris/renderer/tetris_renderer.cpp"
                            "app/tetris/tetrisApp.cpp"
                            "app/tetris/tetrisRoomApp.cpp"
                            "app/tetris/tetrisRemoteApp.cpp"
                            "app/tetris/net/tetris_net.cpp"
                            "app/tetris/net/tetrisRemoteClient.cpp"
                            "app/tetris/net/tetrisTcpHost.cpp"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "."
                                         "../../../main"
                                         "../../../main/elfSdk"
                       REQUIRES lvgl esp_lvgl_adapter fatfs
                                esp_http_server esp_websocket_client json lwip
                                esp_audio_render gmf_audio esp_audio_codec esp_audio_effects gmf_core gmf_fft)
# .so 需要 PIC；关闭异常/RTTI 与主固件一致（插件不含这两者）
target_compile_options(${COMPONENT_LIB} PRIVATE -fPIC -fno-exceptions -fno-rtti)
