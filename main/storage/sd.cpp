#include "sd.hpp"
#include "driver/sdmmc_host.h"

#include <esp_log.h>

#include "vfs.hpp"
#include "fat.hpp"
#include <sdmmc_cmd.h>

#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

constexpr static char TAG[] = "SD";
EXT_RAM_BSS_ATTR static sdmmc_card_t* card = nullptr;

bool mountSd()
{
	if (!testFloor(PrefixSd))
	{
		ESP_LOGW(TAG, "%s is not exsit, can't mount sd card", PrefixSd);
		return false;
	}

	if (card != nullptr)
	{
		ESP_LOGW(TAG, "already mounted, don't mount again");
		return false;
	}

	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	host.slot = SDMMC_HOST_SLOT_0;
	host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

#if SOC_SDMMC_IO_POWER_EXTERNAL
	// Initialize on-chip LDO power control for SDMMC IO (required for ESP32-P4 Slot 0)
	sd_pwr_ctrl_ldo_config_t ldo_config = {
		.ldo_chan_id = 4,	// LDO_VO4
	};
	sd_pwr_ctrl_handle_t pwr_ctrl_handle = nullptr;

	auto ret_ldo = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
	if (ret_ldo != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to create on-chip LDO power control driver (%s)", esp_err_to_name(ret_ldo));
		return false;
	}
	host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
	slot_config.width = 4;

	esp_vfs_fat_sdmmc_mount_config_t mount_config{};

	mount_config.format_if_mount_failed = false;
	mount_config.max_files = 5;
	mount_config.allocation_unit_size = 16 * 1024;

	ESP_LOGI(TAG, "Mounting SD card");
	auto ret = esp_vfs_fat_sdmmc_mount(PrefixSd, &host, &slot_config, &mount_config, &card);

	if (ret != ESP_OK)
	{
		if (ret == ESP_FAIL)
		{
			ESP_LOGE(TAG, "Failed to mount filesystem.");
		}
		else
		{
			ESP_LOGE(TAG, "Failed to initialize the card (%s).", esp_err_to_name(ret));
#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
			check_sd_card_pins(&config, pin_count);
#endif
		}
		return false;
	}
	ESP_LOGI(TAG, "Filesystem mounted");

	sdmmc_card_print_info(stdout, card);
	return true;
}

void unmountSd()
{
	if (card == nullptr)
	{
		ESP_LOGW(TAG, "sd not mount, can't unmount");
		return;
	}
	esp_vfs_fat_sdcard_unmount(PrefixSd, card);
	card = nullptr;
	ESP_LOGI(TAG, "Card unmounted");
}
