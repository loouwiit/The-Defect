idf_component_register(SRCS "main.cpp"
                            "iic/iic.cpp"
                            "task/task.cpp"
                            "wifi/wifi.cpp"
                            "display/display.cpp"
                            "touch/touch.cpp"
                            "app/app.cpp"
                            "app/testApp.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_wifi esp_event esp_netif lwip
                       PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_lvgl_adapter)
