#include "touch.hpp"
#include "esp_log.h"

Touch::Touch(IIC& iic, GPIO reset, uint16_t address, unsigned speed)
{
	// 先探测 GT911 是否在线，避免后续 init 函数因设备不在线而卡死
	if (!iic.detect(address)) {
		ESP_LOGW("Touch", "GT911 not detected at address 0x%02X, skipping init", address);
		handle = nullptr;
		ioHandle = nullptr;
		return;
	}

	esp_lcd_panel_io_i2c_config_t io_config = {
		.dev_addr = address,
		.on_color_trans_done = nullptr,
		.user_ctx = nullptr,
		.control_phase_bytes = 1,
		.dc_bit_offset = 0,
		.lcd_cmd_bits = 16,
		.lcd_param_bits = 0,
		.flags = { .dc_low_on_data = 0, .disable_control_phase = 1 },
		.scl_speed_hz = speed,
	};

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

	esp_err_t ret = esp_lcd_touch_new_i2c_gt911(ioHandle, &tp_cfg, &handle);
	if (ret != ESP_OK) {
		ESP_LOGE("Touch", "Failed to init GT911: %s", esp_err_to_name(ret));
		handle = nullptr;
	}
	ESP_LOGI("Touch", "GT911 initialized successfully");
}

Touch::~Touch()
{
	if (handle) {
		esp_lcd_touch_del(handle);
		esp_lcd_panel_io_del(ioHandle);
	}
}

esp_lcd_touch_handle_t Touch::getHandle()
{
	return handle;
}

bool Touch::update()
{
	if (handle == nullptr)
		return false;

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
