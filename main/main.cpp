/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_log.h"
#include "esp_task.h"

#include "iic/iic.hpp"
#include "gt911/gt911.hpp"

constexpr static char TAG[] = "main";

extern "C" void app_main(void)
{
	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	
	GPIO reset{ GPIO_NUM_46 };
	reset = 0;
	vTaskDelay(100 / portTICK_PERIOD_MS);
	reset = 1;
	vTaskDelay(100 / portTICK_PERIOD_MS);

	Touch touch{ iic };	

	while (true)
	{
		auto data = touch.getPointData();
		if (data.count > 0) {
            ESP_LOGI(TAG, "Touched %d points:", data.count);
            for (int i = 0; i < data.count; i++) {
                ESP_LOGI(TAG, "  [%d] track=%d, (%d, %d), strength=%d",
                         i, data.points[i].track_id,
                         data.points[i].x, data.points[i].y,
                         data.points[i].strength);
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
