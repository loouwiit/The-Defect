#include "screenStream.hpp"

#include "esp_lv_adapter.h"
#include "draw/snapshot/lv_snapshot.h"
#include "draw/lv_draw_buf.h"

#include "esp_jpeg_enc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <cstring>

static const char* TAG = "ScreenStream";

ScreenStream& ScreenStream::instance()
{
	static ScreenStream inst;
	return inst;
}

bool ScreenStream::start(Display* display)
{
	if (m_started) return true;
	if (display == nullptr) return false;
	m_display = display;
	m_started = true;
	ESP_LOGI(TAG, "ScreenStream started");
	return true;
}

void ScreenStream::stop()
{
	m_started = false;
	m_display = nullptr;
}

bool ScreenStream::captureJpeg(uint8_t* outBuf, size_t outBufSize, size_t& outSize)
{
	if (!m_started || outBuf == nullptr || outBufSize == 0) return false;

	// 锁住 LVGL 并获取 snapshot draw buffer
	lv_draw_buf_t* draw_buf = nullptr;
	if (auto guard = m_display->lockGuard())
	{
		lv_obj_t* scr = lv_scr_act();
		draw_buf = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
		if (!draw_buf)
		{
			ESP_LOGE(TAG, "lv_snapshot_take failed");
			return false;
		}
	}
	else
	{
		ESP_LOGE(TAG, "Failed to lock LVGL adapter");
		return false;
	}

	// 配置 JPEG 编码
	jpeg_enc_handle_t jpeg = nullptr;
	jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
	cfg.width = draw_buf->header.w;
	cfg.height = draw_buf->header.h;
	cfg.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
	cfg.task_enable = false; // synchronous for simplicity

	jpeg_error_t err = jpeg_enc_open(&cfg, &jpeg);
	if (err != JPEG_ERR_OK || jpeg == nullptr) {
		ESP_LOGE(TAG, "jpeg_enc_open failed: %d", err);
		lv_draw_buf_destroy(draw_buf);
		return false;
	}

	int out_sz = 0;
	err = jpeg_enc_process(jpeg, (const uint8_t*)draw_buf->data, (int)draw_buf->data_size, outBuf, (int)outBufSize, &out_sz);
	if (err != JPEG_ERR_OK) {
		ESP_LOGE(TAG, "jpeg_enc_process failed: %d", err);
		jpeg_enc_close(jpeg);
		lv_draw_buf_destroy(draw_buf);
		return false;
	}

	outSize = (size_t)out_sz;

	jpeg_enc_close(jpeg);
	lv_draw_buf_destroy(draw_buf);

	return true;
}
