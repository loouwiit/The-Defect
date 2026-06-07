#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include "esp_audio_simple_dec.h"

/**
 * Decoder — 音频解码器
 *
 * 封装 esp_audio_codec 的 Simple Decoder。
 * 内部三缓冲区管理输入数据，零拷贝（仅指针交换）。
 *
 * 用法：
 * @code{cpp}
 *   Decoder::registerAll();
 *
 *   Decoder dec;
 *   dec.open(ESP_AUDIO_SIMPLE_DEC_TYPE_MP3);
 *
 *   uint8_t feed[4096];
 *   int16_t pcm[8192];
 *
 *   while (auto read = file.read(feed, sizeof(feed))) {
 *       size_t got = dec.decode(feed, read, pcm, sizeof(pcm));
 *       if (got > 0) audio.write(pcm, got);
 *   }
 *   dec.flush(pcm, sizeof(pcm), [](void* ctx, const void* d, size_t l) {
 *       ((ES8311*)ctx)->write(d, l);
 *   }, &audio);
 *
 *   dec.close();
 * @endcode
 */
class Decoder
{
public:
	struct Info
	{
		int sampleRate{};
		int channel{};
		int bitsPerSample{};
	};

	Decoder(size_t bufSize = 8192);
	~Decoder();

	Decoder(const Decoder&) = delete;
	Decoder& operator=(const Decoder&) = delete;

	// ─── 全局注册 ─────────────────────────────────────

	static bool registerAll();
	static void unregisterAll();

	// ─── 生命周期 ─────────────────────────────────────

	bool open(int type);
	void close();
	bool isOpen() const { return handle != nullptr; }

	// ─── 解码 ─────────────────────────────────────────

	size_t decode(const void* in, size_t inLen, void* out, size_t outLen);

	using FlushCb = void (*)(void* ctx, const void* data, size_t len);
	void flush(void* out, size_t outLen, FlushCb cb = nullptr, void* ctx = nullptr);

	bool getInfo(Info& info);
	void reset();

private:
	void* handle{};

	// ─── 三缓冲区 ─────────────────────────────────────
	//  三个等大 buf，索引 0/1/2 固定轮转，零拷贝
	//  loadIdx   — 装载槽（caller 写入这里）
	//  readyIdx  — 待消费槽（已装满，-1=无）
	//  activeIdx — 使用中槽（simple_dec 正读这里）
	//  activeOff — 使用中槽内已消费偏移
	//  feedOff   — 装载槽内已写入字节数

	static constexpr int SlotCount = 3;
	uint8_t* buf[SlotCount]{};
	size_t   bufSize{};

	int    loadIdx   = 0;
	int    readyIdx  = -1;
	int    activeIdx = -1;
	size_t activeOff = 0;   // 使用中槽已消费的偏移
	size_t activeLen = 0;   // 使用中槽实际有效数据长度
	size_t feedOff   = 0;   // 装载槽已写入字节数
	size_t readyLen  = 0;   // 待消费槽有效数据长度

	/** 找一个空闲槽作为新的装载槽 */
	int pickFreeSlot();
	/** 提交装载槽到待消费队列 */
	void commitLoad();
	/** 换入下一个到使用中 */
	void swapNext();
};
