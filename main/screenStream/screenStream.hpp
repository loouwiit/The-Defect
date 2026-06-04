#pragma once

#include <cstddef>
#include <cstdint>
#include "display/display.hpp"

/**
 * @brief Screen stream module — captures frame buffer directly from bridge
 *
 * Uses ESP32-P4 hardware JPEG encoder (ESP-IDF esp_driver_jpeg).
 * Hardware performance: 720P@70fps, 1080P@40fps (datasheet).
 * No software encoding overhead, no multi-core task scheduling needed.
 */
class ScreenStream {
public:
	static ScreenStream& instance();

	/**
	 * @brief Start the screen stream module
	 * @param display Pointer to the initialized Display instance
	 * @param frameWidth  Physical frame width in pixels (e.g. 720)
	 * @param frameHeight Physical frame height in pixels (e.g. 1280)
	 * @return true on success
	 */
	bool start(Display* display, uint16_t frameWidth, uint16_t frameHeight);

	/**
	 * @brief Stop the screen stream module
	 */
	void stop();

	/**
	 * @brief Capture the latest available frame as JPEG
	 *
	 * Uses the ESP32-P4 hardware JPEG encoder.
	 *
	 * @param jpegBuffer Output buffer for JPEG data
	 * @param jpegBufSize Size of output buffer
	 * @return Bytes written to jpegBuffer, or 0 on failure / no frame available
	 */
	size_t captureJpeg(uint8_t* jpegBuffer, size_t jpegBufSize);

	/** @brief Check if streaming is active */
	bool isStarted() const { return m_started; }

private:
	ScreenStream() = default;
	~ScreenStream() = default;

	// Non-copyable, non-movable
	ScreenStream(const ScreenStream&) = delete;
	ScreenStream& operator=(const ScreenStream&) = delete;

	/** @brief Static callback invoked by bridge after each complete frame flush */
	static void frameReadyCallback(lv_display_t* disp, void* fb, size_t fbSize, void* userCtx);

	Display* m_display{};
	bool m_started{};

	// ESP32-P4 硬件 JPEG 编码器句柄
	void* m_jpegHandle{};   // jpeg_encoder_handle_t

	// 物理分辨率
	uint16_t m_frameWidth{};
	uint16_t m_frameHeight{};

	// 帧缓冲缓存（从回调中更新）
	void* m_cachedFrame{};
	size_t  m_cachedFrameSize{};
};
