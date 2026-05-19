#pragma once

#include "iic/iic.hpp"
#include "esp_lcd_touch_gt911.h"

#define CONFIG_LCD_HRES 1080
#define CONFIG_LCD_VRES 720

class Touch
{
    using PointData = esp_lcd_touch_point_data_t;

public:
    struct Data
        {
            static constexpr uint8_t MAX_POINTS = 5;
            PointData points[MAX_POINTS];
            uint8_t count = 0;           
        };    
    
    Touch(IIC &iic);

    Touch(Touch&) = delete;
    Touch& operator=(Touch&) = delete;

    Data getPointData();

    bool detect() {return iic.detect(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS) || iic.detect(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);}

    ~Touch();

private:
    IIC &iic;
    esp_lcd_touch_handle_t tp;
    esp_lcd_panel_io_handle_t io_handle;
};