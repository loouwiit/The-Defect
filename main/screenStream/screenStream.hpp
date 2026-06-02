#pragma once

#include <cstddef>
#include <cstdint>
#include "display/display.hpp"
#include <esp_jpeg_enc.h>

/**
 * @brief Screen stream module — captures frame buffer directly from bridge
 *
 * Instead of using lv_snapshot (which re-renders the entire UI), this module
 * reads the front buffer that the esp_lvgl_adapter bridge already maintains.
 * A frame-ready callback is registered with the display manager, invoked after
 * each complete frame flush. captureJpeg() reads the cached buffer directly.
 *
 * Zero rendering overhead when streaming is not active (only a null pointer
 * check per frame in the bridge).
 */
class ScreenStream {
public:
	static ScreenStream& instance();

	/**
	 * @brief Start the screen stream module
	 * @param display Pointer to the initialized Display instance
	 * @param frameWidth  Physical frame width in pixels (e.g. 720)
	 * @param frameHeight Physical frame height in pixels (e.g. 1280)
	 * @param multiCore   Whether to use multiple cores (default: false)
	 * @param coreAffinity If multiCore is true, set JPEG encoding task affinity (default: tskNO_AFFINITY) (bug: tskNO_AFFINITY was not supported)
	 * @return true on success
	 */
	bool start(Display* display, uint16_t frameWidth, uint16_t frameHeight, bool multiCore = false, BaseType_t coreAffinity = tskNO_AFFINITY);

	/**
	 * @brief Stop the screen stream module
	 */
	void stop();

	/**
	 * @brief Capture the latest available frame as JPEG
	 *
	 * Uses the frame buffer cached by the on_frame_ready callback.
	 * No LVGL re-rendering is involved.
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

	// JPEG 编码器
	jpeg_enc_handle_t m_jpegHandle{};

	// 物理分辨率（已旋转到物理方向）
	uint16_t m_frameWidth{};
	uint16_t m_frameHeight{};

	// 帧缓冲缓存（从回调中更新）
	void* m_cachedFrame{};
	size_t  m_cachedFrameSize{};
};
