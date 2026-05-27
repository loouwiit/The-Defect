#include "screenStream.hpp"

#include "esp_lv_adapter.h"
#include "esp_log.h"

#include <cstring>
#include <utility>
#include <task/task.hpp>
#include "display_manager.h"

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
	self->m_cachedFrameSize = fbSize;
}

// ── 启动 ────────────────────────────────────────────────────
bool ScreenStream::start(Display* display, uint16_t frameWidth, uint16_t frameHeight, bool multiCore, BaseType_t coreAffinity)
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

	// 配置 JPEG 编码器
	jpeg_enc_config_t jpegConfig = DEFAULT_JPEG_ENC_CONFIG();
	jpegConfig.width = frameWidth;
	jpegConfig.height = frameHeight;
	jpegConfig.src_type = JPEG_PIXEL_FORMAT_RGB565_LE;
	jpegConfig.task_enable = multiCore;
	jpegConfig.hfm_task_priority = Task::Priority::Verylow;
	jpegConfig.hfm_task_core = coreAffinity;

	auto err = jpeg_enc_open(&jpegConfig, &m_jpegHandle);
	if (err != JPEG_ERR_OK || m_jpegHandle == nullptr)
	{
		ESP_LOGE(TAG, "jpeg_enc_open 失败: %d", err);
		stop();
		return false;
	}

	m_started = true;
	ESP_LOGI(TAG, "ScreenStream 已启动 (%dx%d)", frameWidth, frameHeight);
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

	if (m_jpegHandle) jpeg_enc_close(std::exchange(m_jpegHandle, nullptr));

	m_cachedFrame = nullptr;
	m_cachedFrameSize = 0;
	m_started = false;
	m_display = nullptr;

	ESP_LOGI(TAG, "ScreenStream 已停止");
}

// ── 将最新帧编码为 JPEG ────────────────────────────────────
size_t ScreenStream::captureJpeg(uint8_t* jpegBuffer, size_t jpegBufSize)
{
	if (!m_started || jpegBuffer == nullptr || jpegBufSize == 0)
		return 0;

	if (m_cachedFrame == nullptr || m_cachedFrameSize == 0)
	{
		ESP_LOGW(TAG, "尚无可用帧缓冲");
		return 0;
	}

	int out_sz = 0;
	auto err = jpeg_enc_process(m_jpegHandle,
		static_cast<const uint8_t*>(m_cachedFrame),
		static_cast<int>(m_cachedFrameSize),
		jpegBuffer,
		static_cast<int>(jpegBufSize),
		&out_sz);
	if (err != JPEG_ERR_OK)
	{
		ESP_LOGE(TAG, "jpeg_enc_process 失败: %d", err);
		return 0;
	}

	return static_cast<size_t>(out_sz);
}
