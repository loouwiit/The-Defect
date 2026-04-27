idf_component_register(SRCS "main.cpp"
                            "iic/iic.cpp"
                            "task/task.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_i2c
                       INCLUDE_DIRS "")
