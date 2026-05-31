#include "font.hpp"
#include "esp_log.h"
#include "esp_err.h"
#include <cstdlib>
#include <cstdio>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "storage/vfs.hpp"

static constexpr char TAG[] = "FontLoader";

// VFS 驱动

// 将 "F:path/file" 转换为 "/root/path/file"
static void convert_path(const char* lvglPath, char* out, size_t outSize)
{
	// lvglPath 格式: "F:system/NotoSC.ttf"
	// 去掉 'F:' 前缀，拼接到 /root
	const char* relPath = lvglPath;
	if (relPath[0] == 'F' && relPath[1] == ':')
	{
		relPath = relPath + 2;
	}
	snprintf(out, outSize, "%s/%s", PerfixRoot, relPath);
}

// LVGL VFS 驱动回调
static void* fs_open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode)
{
	char vfsPath[256];
	convert_path(path, vfsPath, sizeof(vfsPath));
	FILE* f = fopen(vfsPath, "rb");
	if (!f)
	{
		ESP_LOGW(TAG, "fopen failed: %s", vfsPath);
		return nullptr;
	}
	return (void*)f;
}

static lv_fs_res_t fs_close_cb(lv_fs_drv_t* drv, void* file_p)
{
	if (file_p)
	{
		fclose((FILE*)file_p);
	}
	return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
	*br = fread(buf, 1, btr, (FILE*)file_p);
	return *br > 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
	int w = SEEK_SET;
	if (whence == LV_FS_SEEK_CUR) w = SEEK_CUR;
	else if (whence == LV_FS_SEEK_END) w = SEEK_END;
	return fseek((FILE*)file_p, pos, w) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos)
{
	*pos = ftell((FILE*)file_p);
	return LV_FS_RES_OK;
}

// 注册 LVGL VFS 驱动器 'F' → /root
static bool register_vfs_driver(void)
{
	static lv_fs_drv_t drv;
	lv_fs_drv_init(&drv);

	drv.letter = 'F';

	drv.open_cb = fs_open_cb;
	drv.close_cb = fs_close_cb;
	drv.read_cb = fs_read_cb;
	drv.seek_cb = fs_seek_cb;
	drv.tell_cb = fs_tell_cb;

	lv_fs_drv_register(&drv);
	ESP_LOGD(TAG, "VFS driver 'F' registered -> %s", PerfixRoot);
	return true;
}

// 字体加载器
FontLoader::FontEntry FontLoader::s_fonts[MaxCount]{};

const lv_font_t* FontLoader::getDefault(FontSize size)
{
	for (int i = 0; i < MaxCount; i++)
		if (s_fonts[i].size == size)
			return s_fonts[i].font;

	return s_fonts[0].font;
}

bool FontLoader::setDefault(const lv_font_t* font, FontSize size)
{
	for (int i = 0; i < MaxCount; i++)
		if (s_fonts[i].size == size || s_fonts[i].font == nullptr)
		{
			s_fonts[i].size = size;
			s_fonts[i].font = font;
			return true;
		}
	ESP_LOGW(TAG, "no slot for size %d", size);
	return false;
}

const lv_font_t* FontLoader::load(const char* vfsPath, uint16_t size)
{
	// VFS 驱动器仅注册一次
	EXT_RAM_BSS_ATTR static bool drvRegistered = register_vfs_driver();
	(void)drvRegistered;

	esp_lv_adapter_ft_font_handle_t handle;
	esp_lv_adapter_ft_font_config_t cfg{
		.name = vfsPath,
		.size = size,
		.style = ESP_LV_ADAPTER_FT_FONT_STYLE_NORMAL,
	};

	auto ret = esp_lv_adapter_ft_font_init(&cfg, &handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "load failed: %s", esp_err_to_name(ret));
		return nullptr;
	}

	auto font = esp_lv_adapter_ft_font_get(handle);
	ESP_LOGI(TAG, "font loaded: %s size %d", vfsPath, size);
	return font;
}
