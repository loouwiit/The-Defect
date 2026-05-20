/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_log.h"
#include "esp_task.h"

#include "iic/iic.hpp"
#include "touch/touch.hpp"

constexpr static char TAG[] = "main";

extern "C" void app_main(void)
{
	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	Touch touch{ iic, {GPIO_NUM_46} };

	while (true)
	{
		touch.update();

		if (auto count = touch.getCount())
		{
			ESP_LOGI(TAG, "Touched %d points:", count);
			for (auto i = 0; i < count; i++)
			{
				auto&& finger = touch[i];

				ESP_LOGI(TAG, "[%d] track=%d, (%d, %d), strength=%d",
					i, finger.track_id,
					finger.x, finger.y,
					finger.strength);
			}
		}
		vTaskDelay(20 / portTICK_PERIOD_MS);
	}
}
