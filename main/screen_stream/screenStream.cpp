#include "screenStream.hpp"

#include "esp_lv_adapter.h"
#include "draw/snapshot/lv_snapshot.h"
#include "draw/lv_draw_buf.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <cstring>
#include <utility>
#include <task/task.hpp>

static const char* TAG = "ScreenStream";

ScreenStream& ScreenStream::instance()
{
	EXT_RAM_BSS_ATTR static ScreenStream inst{};
	return inst;
}

bool ScreenStream::start(Display* display)
{
	if (m_started) return true;
	if (display == nullptr) return false;
	m_display = display;
	m_started = true;
	ESP_LOGI(TAG, "ScreenStream started");

	// 创建截图缓冲区
	imageBuffer = lv_snapshot_create_draw_buf(lv_scr_act(), LV_COLOR_FORMAT_RGB565);
	if (imageBuffer == nullptr)
	{
		ESP_LOGE(TAG, "Failed to create snapshot draw buffer");
		stop();
		return false;
	}

	// 配置 JPEG 编码器
	jpeg_enc_config_t jpegConfig = DEFAULT_JPEG_ENC_CONFIG();
	jpegConfig.width = imageBuffer->header.w;
	jpegConfig.height = imageBuffer->header.h;
	jpegConfig.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
	jpegConfig.task_enable = true;
	jpegConfig.hfm_task_priority = Task::Priority::Low;
	jpegConfig.hfm_task_core = 1 - xPortGetCoreID(); // 使用另一个核心并行加速

	auto err = jpeg_enc_open(&jpegConfig, &jpegHandle);
	if (err != JPEG_ERR_OK || jpegHandle == nullptr)
	{
		ESP_LOGE(TAG, "jpeg_enc_open failed: %d", err);
		stop();
		return false;
	}

	return true;
}

void ScreenStream::stop()
{
	if (jpegHandle) jpeg_enc_close(std::exchange(jpegHandle, nullptr));
	if (imageBuffer) lv_draw_buf_destroy(std::exchange(imageBuffer, nullptr));
	m_started = false;
	m_display = nullptr;
}

size_t ScreenStream::captureJpeg(uint8_t* jpegBuffer, size_t jpegBufSize)
{
	if (!m_started || imageBuffer->data_size == 0 || jpegBuffer == nullptr || jpegBufSize == 0)
		return 0;

	// 锁住 LVGL 并获取 snapshot draw buffer
	if (auto guard = m_display->lockGuard())
	{
		auto ret = lv_snapshot_take_to_draw_buf(lv_scr_act(), LV_COLOR_FORMAT_RGB565, imageBuffer);
		if (ret != LV_RESULT_OK)
		{
			ESP_LOGE(TAG, "lv_snapshot_take failed");
			return 0;
		}
	}
	else
	{
		ESP_LOGE(TAG, "Failed to lock LVGL adapter");
		return 0;
	}

	int out_sz = 0;
	auto err = jpeg_enc_process(jpegHandle, (const uint8_t*)imageBuffer->data, (int)imageBuffer->data_size, jpegBuffer, (int)jpegBufSize, &out_sz);
	if (err != JPEG_ERR_OK)
	{
		ESP_LOGE(TAG, "jpeg_enc_process failed: %d", err);
		return 0;
	}

	return out_sz;
}
