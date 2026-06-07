#include "audio/decoder.hpp"

#include <cstring>
#include <esp_log.h>

#include "esp_audio_simple_dec_default.h"
#include "esp_audio_dec_default.h"

static constexpr char TAG[] = "Decoder";

// ────────────────────────────────────────────────────────────
// 全局注册
// ────────────────────────────────────────────────────────────

bool Decoder::registerAll()
{
	auto ret = esp_audio_dec_register_default();
	if (ret != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "注册解码器失败: %d", ret);
		return false;
	}
	ret = esp_audio_simple_dec_register_default();
	if (ret != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "注册 Simple Decoder 失败: %d", ret);
		return false;
	}
	ESP_LOGI(TAG, "解码器注册完成");
	return true;
}

void Decoder::unregisterAll()
{
	esp_audio_simple_dec_unregister_default();
	esp_audio_dec_unregister_default();
	ESP_LOGI(TAG, "解码器已注销");
}

// ────────────────────────────────────────────────────────────
// 构造 / 析构
// ────────────────────────────────────────────────────────────

Decoder::Decoder(size_t bufSize)
{
	bufSize = std::max(bufSize, size_t(1024));
	this->bufSize = bufSize;
	for (auto& b : buf) {
		b = new uint8_t[bufSize];
	}
}

Decoder::~Decoder()
{
	close();
	for (auto& b : buf) {
		delete[] b;
	}
}

// ────────────────────────────────────────────────────────────
// 三缓冲区：零拷贝轮转
// ────────────────────────────────────────────────────────────

int Decoder::pickFreeSlot()
{
	for (int i = 0; i < SlotCount; i++) {
		if (i != loadIdx && i != readyIdx && i != activeIdx) {
			return i;
		}
	}
	return -1;
}

void Decoder::commitLoad()
{
	if (feedOff == 0) return;
	if (readyIdx >= 0) return;

	readyIdx = loadIdx;
	readyLen = feedOff;
	feedOff  = 0;

	int freeSlot = pickFreeSlot();
	if (freeSlot >= 0) loadIdx = freeSlot;
}

void Decoder::swapNext()
{
	if (readyIdx < 0) return;
	activeIdx = readyIdx;
	activeLen = readyLen;
	readyIdx  = -1;
	activeOff = 0;
}

// ────────────────────────────────────────────────────────────
// 生命周期
// ────────────────────────────────────────────────────────────

bool Decoder::open(int type)
{
	if (handle != nullptr) {
		ESP_LOGW(TAG, "解码器已打开，先 close");
		return false;
	}

	esp_audio_simple_dec_cfg_t cfg = {
		.dec_type      = (esp_audio_simple_dec_type_t)type,
		.dec_cfg       = nullptr,
		.cfg_size      = 0,
		.use_frame_dec = false,
	};

	auto ret = esp_audio_simple_dec_open(&cfg, (esp_audio_simple_dec_handle_t*)&handle);
	if (ret != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "打开解码器失败: %d", ret);
		handle = nullptr;
		return false;
	}

	loadIdx   = 0;
	readyIdx  = -1;
	activeIdx = -1;
	activeOff = 0;
	activeLen = 0;
	feedOff   = 0;
	readyLen  = 0;

	ESP_LOGI(TAG, "解码器已打开 (type=%d)", type);
	return true;
}

void Decoder::close()
{
	if (handle) {
		esp_audio_simple_dec_close((esp_audio_simple_dec_handle_t)handle);
		handle = nullptr;
	}
	loadIdx   = 0;
	readyIdx  = -1;
	activeIdx = -1;
	activeOff = 0;
	activeLen = 0;
	feedOff   = 0;
	readyLen  = 0;
}

// ────────────────────────────────────────────────────────────
// 解码
// ────────────────────────────────────────────────────────────

size_t Decoder::decode(const void* in, size_t inLen, void* out, size_t outLen)
{
	if (!handle || !out || !outLen) return 0;

	// 1. 写入装载槽
	if (in && inLen > 0) {
		size_t copyLen = std::min(inLen, bufSize - feedOff);
		if (copyLen) {
			std::memcpy(buf[loadIdx] + feedOff, in, copyLen);
			feedOff += copyLen;
		}
	}

	// 2. Pipeline 推进 + 循环解码直到有输出或缺数据
	while (true) {
		// 推进 pipeline: 装载槽 → 待消费 → 使用中
		if (readyIdx < 0 && feedOff > 0) commitLoad();
		if (activeIdx < 0 && readyIdx >= 0) swapNext();

		// 没有数据可处理了
		if (activeIdx < 0) return 0;

		uint8_t* activeBuf = buf[activeIdx];
		size_t   avail     = activeLen - activeOff;

		if (avail == 0) {
			activeIdx = -1; activeOff = 0; activeLen = 0;
			if (readyIdx >= 0) swapNext();
			continue;
		}

		esp_audio_simple_dec_raw_t raw = {
			.buffer = activeBuf + activeOff,
			.len    = (uint32_t)avail,
		};
		esp_audio_simple_dec_out_t frame = {
			.buffer = (uint8_t*)out,
			.len    = (uint32_t)outLen,
		};

		auto ret = esp_audio_simple_dec_process(
			(esp_audio_simple_dec_handle_t)handle, &raw, &frame
		);

		if (raw.consumed > 0) {
			activeOff += std::min((size_t)raw.consumed, avail);
		}

		// 使用中槽耗尽 → 准备换入下一个
		if (activeOff >= activeLen) {
			activeIdx = -1; activeOff = 0; activeLen = 0;
		}

		// 有输出 → 返回，剩余数据下次 decode() 继续
		if (frame.decoded_size > 0) {
			return frame.decoded_size;
		}

		// simple_dec 需要更多数据 → 退出让 caller 喂
		if (ret == ESP_AUDIO_ERR_CONTINUE || ret == ESP_AUDIO_ERR_OK) {
			// 如果 active 已耗尽且 ready 有数据，循环会重试
			if (activeIdx >= 0) return 0;  // 等更多数据
			// 否则继续循环（换入 ready）
			continue;
		}

		// 错误
		ESP_LOGW(TAG, "解码返回 %d", ret);
		return 0;
	}
}

// ────────────────────────────────────────────────────────────
// 刷新
// ────────────────────────────────────────────────────────────

void Decoder::flush(void* out, size_t outLen, FlushCb cb, void* ctx)
{
	if (!handle) return;

	// 把装载槽剩余数据推入 pipeline
	if (feedOff > 0 && readyIdx < 0) commitLoad();
	if (activeIdx < 0 && readyIdx >= 0) swapNext();

	uint8_t dummy;  // eos 时需要非空 buffer

	while (true) {
		// 如果 active 有数据，处理剩余数据 + eos
		// 如果 active 已空，仅发 eos 通知 simple_dec 刷新缓存
		bool hasData = (activeIdx >= 0 && activeOff < activeLen);
		uint8_t* bufPtr = hasData ? (buf[activeIdx] + activeOff) : &dummy;
		size_t   bufLen = hasData ? (activeLen - activeOff) : 0;

		esp_audio_simple_dec_raw_t raw = {
			.buffer = bufPtr,
			.len    = (uint32_t)bufLen,
			.eos    = true,
		};
		esp_audio_simple_dec_out_t frame = {
			.buffer = (uint8_t*)out,
			.len    = (uint32_t)outLen,
		};

		auto ret = esp_audio_simple_dec_process(
			(esp_audio_simple_dec_handle_t)handle, &raw, &frame
		);

		if (raw.consumed > 0 && hasData) {
			activeOff += std::min((size_t)raw.consumed, bufLen);
		}

		if (frame.decoded_size > 0 && cb) {
			cb(ctx, out, frame.decoded_size);
		}

		// eos 已发出且无更多输出 → 结束
		if (frame.decoded_size == 0) break;

		// 还有输出？说明 simple_dec 缓存中还有 PCM，继续 flush
	}
}

// ────────────────────────────────────────────────────────────
// 信息查询
// ────────────────────────────────────────────────────────────

bool Decoder::getInfo(Info& info)
{
	if (!handle) return false;
	esp_audio_simple_dec_info_t decInfo{};
	auto ret = esp_audio_simple_dec_get_info(
		(esp_audio_simple_dec_handle_t)handle, &decInfo
	);
	if (ret != ESP_AUDIO_ERR_OK) return false;
	info.sampleRate    = (int)decInfo.sample_rate;
	info.channel       = (int)decInfo.channel;
	info.bitsPerSample = (int)decInfo.bits_per_sample;
	return true;
}

// ────────────────────────────────────────────────────────────
// 重置
// ────────────────────────────────────────────────────────────

void Decoder::reset()
{
	if (handle) {
		esp_audio_simple_dec_reset((esp_audio_simple_dec_handle_t)handle);
	}
	loadIdx   = 0;
	readyIdx  = -1;
	activeIdx = -1;
	activeOff = 0;
	activeLen = 0;
	feedOff   = 0;
	readyLen  = 0;
}
