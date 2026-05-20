#pragma once

#include "iic/iic.hpp"
#include "esp_lcd_touch_gt911.h"

class Touch
{
public:
	using PointData = esp_lcd_touch_point_data_t;

	constexpr static uint8_t Address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
	constexpr static uint8_t AddressAlternative = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;

	constexpr static size_t ResolutionX = 720;
	constexpr static size_t ResolutionY = 1280;

	static constexpr uint8_t MaxPointCount = 5;

	Touch(IIC& iic, GPIO reset = GPIO::NC, uint16_t address = Address, unsigned speed = 100000);

	~Touch();

	bool update();

	uint8_t getCount();

	PointData operator[](uint8_t index);

private:
	esp_lcd_touch_handle_t handle{};
	esp_lcd_panel_io_handle_t ioHandle{};

	PointData pointData[MaxPointCount]{};
	uint8_t count{};
};
