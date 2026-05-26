#pragma once

#include <cstddef>
#include <cstdint>
#include "display/display.hpp"
#include <esp_jpeg_enc.h>

class ScreenStream {
public:
	static ScreenStream& instance();

	/**
	 * @brief Start the screen stream module
	 * @param display Pointer to the initialized Display instance
	 * @return true on success, false on failure (e.g., null display)
	 */
	bool start(Display* display);

	/**
	 * @brief Stop the screen stream module
	 */
	void stop();

	/**
	 * @brief Capture the current screen content as a JPEG image
	 * @param jpegBuffer Pointer to the buffer where the JPEG data will be stored
	 * @param jpegBufSize Size of the provided buffer in bytes
	 * @return The number of bytes written to the JPEG buffer on success, 0 on failure
	 */
	size_t captureJpeg(uint8_t* jpegBuffer, size_t jpegBufSize);

private:
	ScreenStream() = default;
	~ScreenStream() = default;

	Display* m_display{};
	bool m_started{};
	jpeg_enc_handle_t jpegHandle{};
	lv_draw_buf_t* imageBuffer{};
};
