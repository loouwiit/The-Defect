#include "screenStream.hpp"

#include "esp_lv_adapter.h"
#include "esp_log.h"

#include <utility>
#include "display_manager.h"

/* ESP32-P4 硬件 JPEG 编码器 */
#include <driver/jpeg_encode.h>

static constexpr char TAG[] = "ScreenStream";

ScreenStream& ScreenStream::instance()
{
	EXT_RAM_BSS_ATTR static ScreenStream inst{};
	return inst;
}

// ── 静态回调：由 bridge 在每帧 flush 完成后调用 ──────────────
void ScreenStream::frameReadyCallback(lv_display_t* disp, void* fb, size_t fbSize, void* userCtx)
{
	auto* self = static_cast<ScreenStream*>(userCtx);
	if (!self || !fb || fbSize == 0) return;

	self->m_cachedFrame = fb;
}

// ── 启动 ────────────────────────────────────────────────────
bool ScreenStream::start(Display* display, uint16_t frameWidth, uint16_t frameHeight)
{
	if (m_started) return true;
	if (display == nullptr) return false;

	m_display = display;
	m_frameWidth = frameWidth;
	m_frameHeight = frameHeight;

	// 注册 frame-ready 回调
	auto lv_disp = m_display->getLvglDisplay();
	auto ret = display_manager_set_frame_ready_callback(lv_disp, frameReadyCallback, this);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "注册 frame-ready 回调失败: %s", esp_err_to_name(ret));
		return false;
	}

	// 配置 ESP32-P4 硬件 JPEG 编码器引擎
	jpeg_encode_engine_cfg_t eng_cfg = {
		.intr_priority = 0,       // 使用默认中断优先级
		.timeout_ms    = 500,     // 500ms 超时（硬件 720p 约 14ms）
	};

	auto handle = reinterpret_cast<jpeg_encoder_handle_t*>(&m_jpegHandle);
	ret = jpeg_new_encoder_engine(&eng_cfg, handle);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "jpeg_new_encoder_engine 失败: %d", ret);
		stop();
		return false;
	}

	/* 预计算帧缓冲大小（RGB565，每像素 2 字节） */
	m_cachedFrameSize = (size_t)m_frameWidth * m_frameHeight * 2;

	m_started = true;
	ESP_LOGI(TAG, "ScreenStream 已启动 (%dx%d), fbSize=%zu", frameWidth, frameHeight, m_cachedFrameSize);
	return true;
}

// ── 停止 ────────────────────────────────────────────────────
void ScreenStream::stop()
{
	// 注销回调
	if (m_display)
	{
		auto lv_disp = m_display->getLvglDisplay();
		display_manager_set_frame_ready_callback(lv_disp, nullptr, nullptr);
	}

	if (m_jpegHandle)
	{
		auto handle = reinterpret_cast<jpeg_encoder_handle_t>(m_jpegHandle);
		jpeg_del_encoder_engine(handle);
		m_jpegHandle = nullptr;
	}

	m_cachedFrame = nullptr;
	m_cachedFrameSize = 0;
	m_started = false;
	m_display = nullptr;

	ESP_LOGI(TAG, "ScreenStream 已停止");
}

// ── 将最新帧编码为 JPEG（ESP32-P4 硬件加速）───────────────
size_t ScreenStream::captureJpeg(uint8_t* jpegBuffer, size_t jpegBufSize)
{
	if (!m_started || jpegBuffer == nullptr || jpegBufSize == 0)
		return 0;

	if (m_cachedFrame == nullptr || m_cachedFrameSize == 0)
	{
		return 0;
	}

	// 每帧编码配置
	jpeg_encode_cfg_t enc_cfg = {
		.height       = m_frameHeight,
		.width        = m_frameWidth,
		.src_type     = JPEG_ENCODE_IN_FORMAT_RGB565,
		.sub_sample   = JPEG_DOWN_SAMPLING_YUV420,
		.image_quality = 60,
	};

	uint32_t out_size = 0;
	auto handle = reinterpret_cast<jpeg_encoder_handle_t>(m_jpegHandle);
	auto err = jpeg_encoder_process(handle, &enc_cfg,
		static_cast<const uint8_t*>(m_cachedFrame),
		m_cachedFrameSize,
		jpegBuffer,
		jpegBufSize,
		&out_size);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "硬件 JPEG 编码失败: %d", err);
		return 0;
	}

	return out_size;
}
