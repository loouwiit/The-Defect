#include "gt911.hpp"

Touch::Touch(IIC &iic) : iic(iic)
{
    esp_lcd_panel_io_i2c_config_t io_config = {ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, nullptr, nullptr, 1, 0, 16, 0, {0, 1}, 100000};

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(iic.busHandle, &io_config, &io_handle));

    esp_lcd_touch_io_gt911_config_t tp_gt911_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
    };

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = CONFIG_LCD_HRES,
        .y_max = CONFIG_LCD_VRES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &tp_gt911_config,
    };

    esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &tp);
}

Touch::~Touch()
{
    esp_lcd_touch_del(tp);
    esp_lcd_panel_io_del(io_handle);
}

Touch::Data Touch::getPointData()
{
    Data data{};

    if(esp_lcd_touch_read_data(tp)!= ESP_OK){return data;}

    uint8_t point_cnt = 0;
    if (esp_lcd_touch_get_data(tp, data.points, &point_cnt, Data::MAX_POINTS) == ESP_OK) {
        data.count = point_cnt;
    }

    return data;

}