#include "touch.hpp"
#include "utility"

Touch::Touch(IIC& iic, GPIO reset, uint16_t address, unsigned speed)
{
	esp_lcd_panel_io_i2c_config_t io_config = { address, nullptr, nullptr, 1, 0, 16, 0, {0, 1}, speed };

	ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(iic.getBusHandle(), &io_config, &ioHandle));

	esp_lcd_touch_io_gt911_config_t tp_gt911_config = {
		.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
	};

	esp_lcd_touch_config_t tp_cfg{};
	tp_cfg.x_max = ResolutionX;
	tp_cfg.y_max = ResolutionY;
	tp_cfg.rst_gpio_num = reset;
	tp_cfg.int_gpio_num = GPIO::NC;

	tp_cfg.flags.swap_xy = true;
	tp_cfg.flags.mirror_x = true;

	tp_cfg.driver_data = &tp_gt911_config;

	esp_lcd_touch_new_i2c_gt911(ioHandle, &tp_cfg, &handle);
}

Touch::Touch(Touch&& copy)
{
	operator=(std::move(copy));
}

Touch& Touch::operator=(Touch&& copy)
{
	std::swap(handle, copy.handle);
	std::swap(ioHandle, copy.ioHandle);
	std::swap(pointData, copy.pointData);
	std::swap(count, copy.count);
	return *this;
}

Touch::~Touch()
{
	if (handle != nullptr)
		esp_lcd_touch_del(handle);
	if (ioHandle != nullptr)
		esp_lcd_panel_io_del(ioHandle);
}

esp_lcd_touch_handle_t Touch::getHandle()
{
	return handle;
}

bool Touch::update()
{
	if (esp_lcd_touch_read_data(handle) != ESP_OK)
		return false;

	if (esp_lcd_touch_get_data(handle, pointData, &count, MaxPointCount) != ESP_OK)
		return false;

	return true;
}

uint8_t Touch::getCount()
{
	return count;
}

Touch::PointData Touch::operator[](uint8_t index)
{
	return pointData[index];
}
