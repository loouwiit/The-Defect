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

	Touch() = default;
	Touch(IIC& iic, GPIO reset = GPIO::NC, GPIO intterupt = GPIO::NC, uint8_t address = Address, unsigned speed = 100000);
	Touch(Touch&& copy);
	Touch& operator=(Touch&& copy);

	~Touch();

	esp_lcd_touch_handle_t getHandle();

	/// 读取当前全部触点快照（带 track_id 的屏幕坐标），返回实际点数
	/// 复用 esp_lcd_touch_get_data（带锁、坐标已 swap/mirror 转换），任意任务可调
	size_t getPoints(PointData* buf, size_t max);

	bool update();

	uint8_t getCount();

	PointData operator[](uint8_t index);

private:
	esp_lcd_touch_handle_t handle{};
	esp_lcd_panel_io_handle_t ioHandle{};

	PointData pointData[MaxPointCount]{};
	uint8_t count{};
};
