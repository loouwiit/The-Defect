#include <esp_log.h>
#include <nvs_flash.h>

inline void nvsInit()
{
	ESP_LOGI("NVS", "NVS init");
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_LOGI("NVS", "NVS inited");
}
