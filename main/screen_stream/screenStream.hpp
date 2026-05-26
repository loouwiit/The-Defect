#pragma once

#include <cstddef>
#include <cstdint>
#include "display/display.hpp"

class ScreenStream {
public:
	static ScreenStream& instance();

	// 初始化并分配输出缓冲
	bool start(Display* display);

	// 停止并释放资源
	void stop();

	// 同步抓取当前屏幕并编码为 JPEG，outBuf 必须指向足够大的缓冲区
	// 返回 true 表示成功，并通过 outSize 输出 JPEG 大小
	bool captureJpeg(uint8_t* outBuf, size_t outBufSize, size_t& outSize);

private:
	ScreenStream() = default;
	~ScreenStream() = default;

	Display* m_display = nullptr;
	bool m_started = false;
};
