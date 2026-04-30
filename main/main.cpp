/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_log.h"
#include "esp_task.h"

#include "iic/iic.hpp"

constexpr static char TAG[] = "main";

extern "C" void app_main(void)
{
	GPIO reset{ GPIO_NUM_46 };
	reset = 0;
	vTaskDelay(100 / portTICK_PERIOD_MS);
	reset = 1;
	vTaskDelay(100 / portTICK_PERIOD_MS);

	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	while (true)
	{
		if (iic.detect(0x5D))
			ESP_LOGI(TAG, "Device detected at address 0x5D\n");
		else ESP_LOGE(TAG, "Device not detected at address 0x5D\n");

		if (iic.detect(0x14))
			ESP_LOGI(TAG, "Device detected at address 0x14\n");
		else ESP_LOGE(TAG, "Device not detected at address 0x14\n");

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
