#include "Audio.hpp"
#include "audio/ES8311.hpp"

#include <cstring>
#include <cctype>

#include "esp_log.h"
#include "esp_audio_dec_default.h"
#include "esp_err.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_ae_rate_cvt.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_timer.h"

static constexpr char TAG[] = "Audio";

// ══════════════════════════════════════════════
//  AudioHandle::Impl
// ══════════════════════════════════════════════

AudioHandle::Impl::~Impl()
{
	// 由持有者（AudioHandle 或 task）在适当时机触发清理
	if (stream) {
		esp_audio_render_stream_close(stream);
		if (manager) {
			manager->freeStreamId(streamId);
		}
		stream = nullptr;
	}
	if (dec) {
		esp_audio_dec_close(dec);
		dec = nullptr;
	}
	if (file.isOpen()) {
		file.close();
	}
	delete[] encBuf;  encBuf = nullptr;
	delete[] pcmBuf;  pcmBuf = nullptr;
}

void AudioHandle::Impl::taskFunc(void* arg)
{
	// 取得 shared_ptr，确保任务运行期间 Impl 不被销毁
	auto self = std::move(*static_cast<std::shared_ptr<Impl>*>(arg));
	delete static_cast<std::shared_ptr<Impl>*>(arg);

	self->taskLoop();

	// 任务结束：清理
	// pause 已关闭 stream 时不再重复关闭，但始终释放 stream ID
	if (self->stream && !self->streamClosedByPause) {
		esp_audio_render_stream_close(self->stream);
	}
	self->stream = nullptr;
	if (self->manager) {
		self->manager->freeStreamId(self->streamId);
	}
	if (self->sampleRef) {
		self->sampleRef->_releaseInstance();
		self->sampleRef = nullptr;
	}
	if (self->dec) {
		esp_audio_dec_close(self->dec);
		self->dec = nullptr;
	}
	if (self->file.isOpen()) {
		self->file.close();
	}

	self->running.store(false);

	// 主动释放对 Impl 的引用，允许析构
	self.reset();

	vTaskDelete(NULL);
}

void AudioHandle::Impl::taskLoop()
{
	// ── PCM buffer mode：直接从缓存流式输出，跳过文件 I/O 和解码 ──
	if (pcmBuffer) {
		inputInfo = {
			.sample_rate = pcmSampleRate,
			.bits_per_sample = 16,
			.channel = pcmChannels,
		};
		if (esp_audio_render_stream_open(stream, &inputInfo) != ESP_AUDIO_RENDER_ERR_OK) {
			ESP_LOGE(TAG, "stream_open 失败");
			return;
		}
		const size_t frameBytes = sizeof(int16_t) * pcmChannels;
		// ringbuffer ~11520 字节，每批不超过 4KB（~21ms @44100/16/2）避免超时丢数据
		constexpr size_t MaxBatchBytes = 4096;
		const size_t maxBatchFrames = MaxBatchBytes / frameBytes;
		m_batchTick = esp_timer_get_time();
		while (!stopRequested.load()) {
			if (pauseRequested.load()) {
				if (stream) {
					esp_audio_render_stream_close(stream);
					streamClosedByPause = true;
				}
				while (pauseRequested.load() && !stopRequested.load())
					vTaskDelay(pdMS_TO_TICKS(50));
				if (!stopRequested.load() && stream) {
					esp_audio_render_stream_open(stream, &inputInfo);
					streamClosedByPause = false;
				}
				continue;
			}
			size_t remain = pcmTotalFrames - pcmReadPos;
			if (remain == 0) {
				if (loop) { pcmReadPos = 0; continue; }
				break;
			}
			size_t batch = std::min(remain, maxBatchFrames);
			memcpy(pcmBuf, pcmBuffer + pcmReadPos * pcmChannels, batch * frameBytes);
			pcmReadPos += batch;
			applyVolume(reinterpret_cast<int16_t*>(pcmBuf), batch * pcmChannels, volume.load());
			esp_audio_render_stream_write(stream, pcmBuf, batch * frameBytes);
			// 自适应限速
			uint32_t paceMs = batch * 1000 / pcmSampleRate;
			uint32_t elapsed = (esp_timer_get_time() - m_batchTick) / 1000;
			if (paceMs > elapsed + 1) vTaskDelay(pdMS_TO_TICKS(paceMs - elapsed - 1));
			m_batchTick = esp_timer_get_time();
		}
		// 等待 ringbuffer 排空后再关闭流
		if (!stopRequested.load()) {
			vTaskDelay(pdMS_TO_TICKS(pcmBufSize * 1000 / (pcmSampleRate * frameBytes) + 30));
		}
		return;
	}

	// ── 1. 解码第一帧，获取音频信息（文件模式） ──
	size_t firstRead = file.read(encBuf, EncBufSize);
	if (firstRead == 0) {
		ESP_LOGE(TAG, "无法读取音频文件");
		return;
	}

	esp_audio_dec_in_raw_t raw{};
	raw.buffer = encBuf;
	raw.len = firstRead;
	esp_audio_dec_out_frame_t frame{};
	frame.buffer = pcmBuf;
	frame.len = pcmBufSize;

	auto ret = esp_audio_dec_process(dec, &raw, &frame);

	// 处理缓冲区大小不足：按 needed_size 重新分配并重试
	if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && frame.needed_size > pcmBufSize) {
		ESP_LOGI(TAG, "PCM 缓冲区从 %u 扩展到 %lu", pcmBufSize, frame.needed_size);
		delete[] pcmBuf;
		pcmBufSize = frame.needed_size;
		pcmBuf = new uint8_t[pcmBufSize];
		if (pcmBuf) {
			frame.buffer = pcmBuf;
			frame.len = pcmBufSize;
			// raw 未被消耗，直接用相同输入重试
			raw.buffer = encBuf;
			raw.len = firstRead;
			ret = esp_audio_dec_process(dec, &raw, &frame);
		}
	}

	if (ret != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "解码器初始化失败: %d", ret);
		return;
	}

	// 获取音频参数
	esp_audio_dec_info_t info{};
	if (esp_audio_dec_get_info(dec, &info) == ESP_AUDIO_ERR_OK) {
		inputInfo.sample_rate = info.sample_rate;
		inputInfo.bits_per_sample = info.bits_per_sample;
		inputInfo.channel = info.channel;
		ESP_LOGI(TAG, "音频参数: %lu Hz, %u bit, %u ch",
			inputInfo.sample_rate, inputInfo.bits_per_sample, inputInfo.channel);
	}
	else {
		// fallback: 假设 44100/16/立体声
		inputInfo = { 44100, 16, 2 };
	}

	// ── 2. Render 会自动做 rate_cvt/ch_cvt/bit_cvt ──
	if (esp_audio_render_stream_open(stream, &inputInfo) != ESP_AUDIO_RENDER_ERR_OK) {
		ESP_LOGE(TAG, "stream_open 失败");
		return;
	}

	// ── 3. 设置初始混音增益 ──
	//  文档限制 set_mixer_gain 必须在 open 前调用，所以此处保持默认增益。
	//  运行时音量通过 applyVolume() 控制。

	// ── 4. 更新已消耗位置 ──
	raw.buffer += raw.consumed;
	raw.len -= raw.consumed;

	// ── 5. 主解码循环 ──
	while (!stopRequested.load()) {
		// 暂停处理：关闭 stream → CLOSED → ES8311::close()
		// 复用同一 stream handle，恢复时 reopen 即可
		if (pauseRequested.load()) {
			if (stream) {
				esp_audio_render_stream_close(stream);
				streamClosedByPause = true;
			}
			// 等待恢复或停止
			while (pauseRequested.load() && !stopRequested.load()) {
				vTaskDelay(pdMS_TO_TICKS(50));
			}
			// 重新打开 stream → OPENED → ES8311::open()
			if (!stopRequested.load() && stream) {
				esp_audio_render_stream_open(stream, &inputInfo);
				streamClosedByPause = false;  // 已重新打开
			}
			continue;
		}

		// 5a. 如果缓冲区有剩余未消耗数据，移到前端
		if (raw.len > 0 && raw.buffer != encBuf) {
			memmove(encBuf, raw.buffer, raw.len);
		}
		raw.buffer = encBuf;

		// 5b. 从文件补充数据
		if (raw.len < EncBufSize) {
			size_t br = file.read(encBuf + raw.len, EncBufSize - raw.len);
			raw.len += br;
		}

		// 5c. 文件读完
		if (raw.len == 0) {
			if (loop) {
				file.setOffset(0, IFile::OffsetMode::Begin);
				esp_audio_dec_reset(dec);
				raw = {};
				raw.buffer = encBuf;
				raw.len = 0;
				continue;
			}
			break;
		}

		// 5d. 解码
		frame.buffer = pcmBuf;
		frame.len = pcmBufSize;
		ret = esp_audio_dec_process(dec, &raw, &frame);

		if (ret == ESP_AUDIO_ERR_OK) {
			raw.buffer += raw.consumed;
			raw.len -= raw.consumed;

			// 应用运行时音量
			applyVolume(reinterpret_cast<int16_t*>(pcmBuf),
				frame.decoded_size / sizeof(int16_t),
				volume.load());

			// 写入 render（多流模式 → ringbuffer）
			esp_audio_render_stream_write(stream, pcmBuf, frame.decoded_size);
		}
		else if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
			if (frame.needed_size > pcmBufSize) {
				ESP_LOGW(TAG, "PCM 缓冲区从 %u 扩展到 %lu", pcmBufSize, frame.needed_size);
				auto* newBuf = new uint8_t[frame.needed_size];
				if (newBuf) {
					delete[] pcmBuf;
					pcmBuf = newBuf;
					pcmBufSize = frame.needed_size;
				}
			}
			// raw 未被消耗，重试
			frame.buffer = pcmBuf;
			frame.len = pcmBufSize;
			ret = esp_audio_dec_process(dec, &raw, &frame);
			if (ret == ESP_AUDIO_ERR_OK) {
				raw.buffer += raw.consumed;
				raw.len -= raw.consumed;
				applyVolume(reinterpret_cast<int16_t*>(pcmBuf),
					frame.decoded_size / sizeof(int16_t),
					volume.load());
				esp_audio_render_stream_write(stream, pcmBuf, frame.decoded_size);
			}
			else {
				ESP_LOGW(TAG, "重试后仍解码错误: %d, 跳过", ret);
				raw.buffer += raw.consumed;
				raw.len -= raw.consumed;
			}
		}
		else {
			ESP_LOGW(TAG, "解码错误: %d, 跳过", ret);
			raw.buffer += raw.consumed;
			raw.len -= raw.consumed;
		}
	}

	// taskFunc 继续清理
}

void AudioHandle::Impl::applyVolume(int16_t* samples, size_t count, float vol)
{
	if (vol >= 1.0f - 0.001f) return;          // 满音量，跳过
	if (vol <= 0.001f) {
		memset(samples, 0, count * sizeof(int16_t));
		return;
	}
	for (size_t i = 0; i < count; ++i) {
		samples[i] = static_cast<int16_t>(samples[i] * vol);
	}
}

// ══════════════════════════════════════════════
//  AudioHandle
// ══════════════════════════════════════════════

AudioHandle::~AudioHandle()
{
	if (!impl) return;

	// 已 detach：handle 析构不干预播放
	if (impl->detached) return;

	// 绑定模式：发停止信号并等待
	if (impl->running.load()) {
		impl->stopRequested.store(true);
		// 等待解码任务退出
		while (impl->running.load()) {
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
}

AudioHandle::AudioHandle(AudioHandle&& move)
{
	*this = std::move(move);
}

AudioHandle& AudioHandle::operator=(AudioHandle&& move)
{
	std::swap(move.impl, impl);
	return *this;
}

AudioHandle& AudioHandle::setLoop(bool loop)
{
	if (impl) impl->loop = loop;
	return *this;
}

AudioHandle& AudioHandle::setVolume(float volume)
{
	if (impl) {
		if (volume < 0.0f) volume = 0.0f;
		if (volume > 1.0f) volume = 1.0f;
		impl->volume.store(volume);
	}
	return *this;
}

float AudioHandle::getVolume() const
{
	return impl ? impl->volume.load() : 0.0f;
}

void AudioHandle::play()
{
	if (!impl || impl->started) return;
	impl->started = true;

	// 从 Audio manager 获取 stream
	esp_audio_render_stream_get(impl->manager->render,
		impl->streamId,
		&impl->stream);
	if (!impl->stream) {
		ESP_LOGE(TAG, "无法获取 render stream %u", impl->streamId);
		// 流获取失败必须立即释放 stream ID
		if (impl->manager)
			impl->manager->freeStreamId(impl->streamId);
		impl->streamId = 0xFF;
		return;
	}

	// PCM buffer mode：无需 encBuf（无需从文件读+解码）
	if (!impl->pcmBuffer) {
		impl->encBuf = new (std::nothrow) uint8_t[Impl::EncBufSize];
		if (!impl->encBuf) {
			ESP_LOGE(TAG, "分配编码缓冲区失败");
			return;
		}
	}
	impl->pcmBuf = new (std::nothrow) uint8_t[Impl::PcmBufSize];
	impl->pcmBufSize = Impl::PcmBufSize;
	if (!impl->pcmBuf) {
		ESP_LOGE(TAG, "分配解码缓冲区失败");
		delete[] impl->encBuf; impl->encBuf = nullptr;
		return;
	}

	// 创建解码任务（通过 shared_ptr 传参，保证 detach 后 Impl 存活）
	auto taskArg = new std::shared_ptr<Impl>(impl);
	if (xTaskCreate(Impl::taskFunc, "audio_dec",
		4096,               // 栈大小 4KB
		taskArg,
		configMAX_PRIORITIES - 2,
		&impl->task) != pdPASS)
	{
		ESP_LOGE(TAG, "创建解码任务失败");
		delete taskArg;
		delete[] impl->encBuf; impl->encBuf = nullptr;
		delete[] impl->pcmBuf; impl->pcmBuf = nullptr;
		return;
	}

	impl->running.store(true);
	ESP_LOGI(TAG, "播放开始 stream=%u", impl->streamId);
}

void AudioHandle::pause()
{
	if (impl && impl->running.load()) {
		impl->pauseRequested.store(true);
		ESP_LOGI(TAG, "暂停 stream=%u", impl->streamId);
	}
}

void AudioHandle::resume()
{
	if (impl) {
		impl->pauseRequested.store(false);
		ESP_LOGI(TAG, "恢复 stream=%u", impl->streamId);
	}
}

void AudioHandle::stop()
{
	if (!impl || !impl->running.load()) return;

	impl->stopRequested.store(true);
	while (impl->running.load()) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void AudioHandle::detach()
{
	if (!impl || impl->detached) return;
	impl->detached = true;

	// 如果还没 play()，自动开始
	if (!impl->started) {
		play();
	}
}

// ══════════════════════════════════════════════
//  AudioSample
// ══════════════════════════════════════════════

void AudioSample::unload()
{
	delete[] m_data;
	m_data = nullptr;
	m_frames = 0;
	m_sampleRate = 0;
	m_channels = 0;
}

bool AudioSample::load(const char* path)
{
	unload();

	IFile file;
	if (!file.open(path)) {
		ESP_LOGE(TAG, "无法打开: %s", path);
		return false;
	}

	auto type = Audio::detectFormat(path);
	if (type == ESP_AUDIO_TYPE_UNSUPPORT) {
		ESP_LOGE(TAG, "不支持的格式: %s", path);
		file.close();
		return false;
	}

	esp_audio_dec_handle_t dec{};
	esp_audio_dec_cfg_t decCfg{};
	decCfg.type = type;
	if (esp_audio_dec_open(&decCfg, &dec) != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "解码器创建失败");
		file.close();
		return false;
	}

	// 分配临时编解码缓冲
	constexpr size_t BufSize = 16384;
	auto* encBuf = new uint8_t[8192];
	auto* pcmBuf = new uint8_t[BufSize];
	size_t pcmCapacity = 0;
	uint8_t* pcmData = nullptr;
	size_t pcmSize = 0;

	// 第一帧
	size_t firstRead = file.read(encBuf, 8192);
	if (firstRead == 0) { goto cleanup; }

	{
		esp_audio_dec_in_raw_t raw{};
		raw.buffer = encBuf;
		raw.len = firstRead;
		esp_audio_dec_out_frame_t frame{};
		frame.buffer = pcmBuf;
		frame.len = BufSize;
		auto ret = esp_audio_dec_process(dec, &raw, &frame);

		if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && frame.needed_size > BufSize) {
			delete[] pcmBuf;
			pcmBuf = new uint8_t[frame.needed_size];
			frame.buffer = pcmBuf;
			frame.len = frame.needed_size;
			raw.buffer = encBuf;
			raw.len = firstRead;
			ret = esp_audio_dec_process(dec, &raw, &frame);
		}
		if (ret != ESP_AUDIO_ERR_OK) { goto cleanup; }

		// 获取音频参数
		esp_audio_dec_info_t info{};
		if (esp_audio_dec_get_info(dec, &info) == ESP_AUDIO_ERR_OK) {
			m_sampleRate = info.sample_rate;
			m_channels = info.channel;
		}
		else {
			m_sampleRate = 44100;
			m_channels = 2;
		}

		// 初始分配
		pcmCapacity = frame.decoded_size * 4;
		pcmData = new uint8_t[pcmCapacity];
		memcpy(pcmData, frame.buffer, frame.decoded_size);
		pcmSize = frame.decoded_size;

		raw.buffer += raw.consumed;
		raw.len -= raw.consumed;

		// 继续解码剩余数据（与 taskLoop 相同的缓冲区管理逻辑）
		while (true) {
			// 将未消耗数据移到缓冲区前端，避免碎片化
			if (raw.len > 0 && raw.buffer != encBuf) {
				memmove(encBuf, raw.buffer, raw.len);
			}
			raw.buffer = encBuf;

			// 补充数据
			if (raw.len < 8192) {
				size_t br = file.read(encBuf + raw.len, 8192 - raw.len);
				raw.len += br;
			}

			// 文件读完
			if (raw.len == 0) break;

			frame.buffer = pcmBuf;
			frame.len = BufSize;
			ret = esp_audio_dec_process(dec, &raw, &frame);
			if (ret == ESP_AUDIO_ERR_OK) {
				if (pcmSize + frame.decoded_size > pcmCapacity) {
					pcmCapacity *= 2;
					auto* newData = new uint8_t[pcmCapacity];
					memcpy(newData, pcmData, pcmSize);
					delete[] pcmData;
					pcmData = newData;
				}
				memcpy(pcmData + pcmSize, frame.buffer, frame.decoded_size);
				pcmSize += frame.decoded_size;
				raw.buffer += raw.consumed;
				raw.len -= raw.consumed;
			}
			else {
				ESP_LOGW(TAG, "解码错误 %d, 跳过", ret);
				raw.buffer += raw.consumed;
				raw.len -= raw.consumed;
			}
		}
	}

	m_data = reinterpret_cast<int16_t*>(pcmData);
	m_frames = pcmSize / (sizeof(int16_t) * m_channels);

	// ── 重采样到 48000Hz（与 render 输出匹配，播放时 processor 直通零缓冲） ──
	if (m_sampleRate != 48000) {
		esp_ae_rate_cvt_cfg_t rsCfg = {
			.src_rate = m_sampleRate,
			.dest_rate = 48000,
			.channel = m_channels,
			.bits_per_sample = 16,
			.complexity = 3,
			.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
		};
		esp_ae_rate_cvt_handle_t rsHd = nullptr;
		if (esp_ae_rate_cvt_open(&rsCfg, &rsHd) == ESP_AE_ERR_OK) {
			uint32_t maxOut = 0;
			esp_ae_rate_cvt_get_max_out_sample_num(rsHd, 2048, &maxOut);
			// 64 位计算：m_frames(uint32) × 48000 在 32 位算术中会溢出
			size_t maxDstFrames = (size_t)m_frames * 48000 / m_sampleRate + 1024;
			auto* dst = new int16_t[maxDstFrames * m_channels];
			size_t dstPos = 0;
			constexpr uint32_t Chunk = 2048;
			auto* chunkBuf = new int16_t[Chunk * m_channels];
			auto* outBuf = new int16_t[maxOut * m_channels];
			// 重采样循环
			size_t srcPos = 0;
			while (srcPos < m_frames) {
				uint32_t nr = std::min(Chunk, (uint32_t)(m_frames - srcPos));
				memcpy(chunkBuf, m_data + srcPos * m_channels, nr * sizeof(int16_t) * m_channels);
				uint32_t outSamples = maxOut;
				if (esp_ae_rate_cvt_process(rsHd, (esp_ae_sample_t)chunkBuf, nr,
					(esp_ae_sample_t)outBuf, &outSamples) == ESP_AE_ERR_OK
					&& outSamples > 0) {
					memcpy(dst + dstPos * m_channels, outBuf, outSamples * sizeof(int16_t) * m_channels);
					dstPos += outSamples;
				}
				srcPos += nr;
			}
			esp_ae_rate_cvt_close(rsHd);
			delete[] m_data;
			delete[] chunkBuf;
			delete[] outBuf;
			m_data = dst;
			m_frames = dstPos;
			m_sampleRate = 48000;
			ESP_LOGI(TAG, "重采样: %lu→48000 Hz, %lu frames", rsCfg.src_rate, m_frames);
		}
		else {
			ESP_LOGW(TAG, "重采样失败, 保留原采样率 %lu Hz", m_sampleRate);
		}
	}

	ESP_LOGI(TAG, "缓存已加载: %s (%lu frames, %lu Hz, %u ch, %zu KB)",
		path, m_frames, m_sampleRate, m_channels, m_frames * sizeof(int16_t) * m_channels / 1024);

	delete[] encBuf;
	delete[] pcmBuf;
	esp_audio_dec_close(dec);
	file.close();
	return true;

cleanup:
	ESP_LOGE(TAG, "加载失败: %s", path);
	delete[] encBuf;
	delete[] pcmBuf;
	delete[] pcmData;
	if (dec) esp_audio_dec_close(dec);
	file.close();
	return false;
}

AudioHandle AudioSample::play(bool loop)
{
	return Audio::play(*this, loop);
}

// ══════════════════════════════════════════════
//  Audio
// ══════════════════════════════════════════════

Audio& Audio::instance()
{
	static Audio inst;
	return inst;
}

void Audio::setMasterVolume(int percent)
{
	auto& self = instance();
	self.m_volume = percent;
	if (self.codec) self.codec->setVolume(percent);
}

int Audio::getMasterVolume()
{
	auto* c = instance().codec;
	return c ? c->getVolume() : 0;
}

void Audio::saveVolumeToNvs()
{
	nvs_handle handle;
	if (nvs_open("audio", NVS_READWRITE, &handle) != ESP_OK)
		return;

	int vol = getMasterVolume();
	nvs_set_u8(handle, "volume", (uint8_t)vol);
	nvs_commit(handle);
	nvs_close(handle);
	ESP_LOGI(TAG, "音量已保存: %d%%", vol);
}

void Audio::loadVolumeFromNvs()
{
	nvs_handle handle;
	if (nvs_open("audio", NVS_READONLY, &handle) != ESP_OK)
		return;

	uint8_t val = 100;
	esp_err_t err = nvs_get_u8(handle, "volume", &val);
	nvs_close(handle);

	if (err != ESP_OK) {
		ESP_LOGI(TAG, "NVS 无音量记录，使用默认值 100%%");
		return;
	}

	setMasterVolume(val);
	ESP_LOGI(TAG, "从 NVS 恢复音量: %d%%", val);
}

bool Audio::init(ES8311& codec)
{
	if (initialized) return true;

	this->codec = &codec;

	// 注册解码器
	auto err = esp_audio_dec_register_default();
	if (err != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "注册解码器失败: %d", err);
		return false;
	}
	ESP_LOGI(TAG, "解码器已注册");

	// 初始化 GMF 元素池
	if (esp_gmf_pool_init(&pool) != ESP_GMF_ERR_OK) {
		ESP_LOGE(TAG, "GMF pool 初始化失败");
		return false;
	}
	ESP_LOGI(TAG, "GMF pool 已初始化");

	// 注册 per-stream processor 元素（render 自动用它们做格式转换）
	{
		esp_gmf_element_handle_t el = nullptr;

		esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
		ch_cvt_cfg.sample_rate = 48000;
		ch_cvt_cfg.bits_per_sample = 16;
		ch_cvt_cfg.src_ch = 1;   // 输入可能是单声道
		ch_cvt_cfg.dest_ch = 2;   // 统一转立体声
		if (esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
			esp_gmf_pool_register_element(pool, el, nullptr);
		}

		esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
		bit_cvt_cfg.sample_rate = 48000;
		bit_cvt_cfg.channel = 2;
		bit_cvt_cfg.src_bits = 16;
		bit_cvt_cfg.dest_bits = 16;
		if (esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
			esp_gmf_pool_register_element(pool, el, nullptr);
		}

		esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
		rate_cvt_cfg.src_rate = 44100;
		rate_cvt_cfg.dest_rate = 48000;
		rate_cvt_cfg.channel = 2;
		rate_cvt_cfg.bits_per_sample = 16;
		if (esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
			esp_gmf_pool_register_element(pool, el, nullptr);
		}
	}

	// 创建 audio render
	esp_audio_render_cfg_t cfg{};
	cfg.max_stream_num = MaxStreamNum;
	cfg.out_writer = outWriter;
	cfg.out_ctx = this;
	cfg.out_sample_info = {
		.sample_rate = 48000,
		.bits_per_sample = 16,
		.channel = 2,
	};
	cfg.pool = pool;
	cfg.process_period = 20;

	if (esp_audio_render_create(&cfg, &render) != ESP_AUDIO_RENDER_ERR_OK) {
		ESP_LOGE(TAG, "audio render 创建失败");
		esp_gmf_pool_deinit(pool);
		pool = nullptr;
		return false;
	}
	ESP_LOGI(TAG, "audio render 已创建 (max_stream=%u)", MaxStreamNum);

	// 注册 render 事件回调：自动管理 ES8311 开/关
	esp_audio_render_set_event_cb(render, onRenderEvent, this);

	initialized = true;
	return true;
}

void Audio::deinit()
{
	if (!initialized) return;

	if (render) {
		esp_audio_render_destroy(render);
		render = nullptr;
	}
	if (pool) {
		esp_gmf_pool_deinit(pool);
		pool = nullptr;
	}
	esp_audio_dec_unregister_default();

	initialized = false;
	codec = nullptr;
	ESP_LOGI(TAG, "音频已反初始化");
}

AudioHandle Audio::play(const char* path)
{
	auto& self = instance();
	if (!self.initialized) {
		ESP_LOGE(TAG, "Audio 未初始化");
		return {};
	}

	auto impl = std::make_shared<AudioHandle::Impl>();
	impl->manager = &self;

	// 检测格式
	auto type = detectFormat(path);
	if (type == ESP_AUDIO_TYPE_UNSUPPORT) {
		ESP_LOGE(TAG, "不支持的音频格式: %s", path);
		return {};
	}

	// 打开文件
	if (!impl->file.open(path)) {
		ESP_LOGE(TAG, "无法打开文件: %s", path);
		return {};
	}

	// 创建解码器
	esp_audio_dec_cfg_t decCfg{};
	decCfg.type = type;
	if (esp_audio_dec_open(&decCfg, &impl->dec) != ESP_AUDIO_ERR_OK) {
		ESP_LOGE(TAG, "解码器创建失败 type=%d", type);
		impl->file.close();
		return {};
	}

	// 分配 stream ID
	auto sid = self.allocStreamId();
	if (sid == 0xFF) {
		ESP_LOGE(TAG, "无空闲音频流");
		impl->file.close();
		esp_audio_dec_close(impl->dec);
		return {};
	}
	impl->streamId = sid;

	ESP_LOGI(TAG, "play: %s (type=%d, stream=%u)", path, type, sid);

	return AudioHandle(std::move(impl));
}

AudioHandle Audio::play(const AudioSample& sample, bool loop)
{
	auto& self = instance();
	if (!self.initialized || !sample.isValid()) {
		ESP_LOGE(TAG, "Audio 未初始化或 AudioSample 无效");
		return {};
	}

	auto impl = std::make_shared<AudioHandle::Impl>();
	impl->manager = &self;
	impl->pcmBuffer = sample.m_data;
	impl->pcmTotalFrames = sample.m_frames;
	impl->pcmSampleRate = sample.m_sampleRate;
	impl->pcmChannels = sample.m_channels;
	impl->loop = loop;
	impl->sampleRef = const_cast<AudioSample*>(&sample);
	sample.m_activeCount.fetch_add(1);

	auto sid = self.allocStreamId();
	if (sid == 0xFF) {
		ESP_LOGE(TAG, "无空闲音频流");
		// 流分配失败必须回退 activeCount
		sample.m_activeCount.fetch_sub(1);
		return {};
	}
	impl->streamId = sid;

	ESP_LOGI(TAG, "play sample: %lu frames, %lu Hz, %u ch, loop=%d",
		sample.m_frames, sample.m_sampleRate, sample.m_channels, loop);

	auto handle = AudioHandle(std::move(impl));
	handle.play();
	handle.detach();  // 防止 NRVO 未发生时局部析构误 stop task
	return handle;
}

int Audio::usedStreamCount()
{
	Audio& self = instance();
	Lock lock(self.m_streamMutex);
	uint16_t mask = self.streamUsedMask;
	int cnt = 0;
	while (mask) { cnt += mask & 1; mask >>= 1; }
	return cnt;
}

// ── 流分配 ──

esp_audio_render_stream_id_t Audio::allocStreamId()
{
	// 多个解码 task 并发启动需互斥分配
	Lock lock(m_streamMutex);
	for (uint8_t i = 0; i < MaxStreamNum; ++i) {
		if (!(streamUsedMask & (1 << i))) {
			streamUsedMask |= (1 << i);
			return i;
		}
	}
	return 0xFF;
}

void Audio::freeStreamId(esp_audio_render_stream_id_t id)
{
	Lock lock(m_streamMutex);
	if (id < MaxStreamNum) {
		streamUsedMask &= ~(1 << id);
	}
}

int Audio::onRenderEvent(esp_audio_render_event_type_t event, void* ctx)
{
	auto* self = static_cast<Audio*>(ctx);
	if (!self || !self->codec) return -1;

	if (event == ESP_AUDIO_RENDER_EVENT_TYPE_OPENED) {
		Lock lock(self->m_codecMutex);
		esp_codec_dev_sample_info_t info{};
		info.bits_per_sample = 16;
		info.channel = 2;
		info.sample_rate = 48000;
		if (self->codec->open(&info)) {
			// 恢复音量（close/open 后编解码器可能丢失音量寄存器）
			self->codec->setVolume(self->m_volume);
		}
	}
	else if (event == ESP_AUDIO_RENDER_EVENT_TYPE_CLOSED) {
		Lock lock(self->m_codecMutex);
		self->codec->close();
	}
	return 0;
}

int Audio::outWriter(uint8_t* pcm, uint32_t size, void* ctx)
{
	auto* self = static_cast<Audio*>(ctx);
	if (!self || !self->codec) return -1;

	self->codec->write(pcm, size);
	return 0;  // render 期望 0 表示成功
}

esp_audio_type_t Audio::detectFormat(const char* path)
{
	// 找到最后一个 '.'
	const char* dot = std::strrchr(path, '.');
	if (!dot) return ESP_AUDIO_TYPE_PCM;

	// 转小写比较
	auto eq = [](const char* a, const char* b) -> bool {
		while (*a && *b) {
			if (std::tolower(static_cast<unsigned char>(*a)) !=
				std::tolower(static_cast<unsigned char>(*b)))
				return false;
			++a; ++b;
		}
		return *a == *b;
		};

	const char* ext = dot + 1;

	if (eq(ext, "mp3"))  return ESP_AUDIO_TYPE_MP3;
	if (eq(ext, "wav"))  return ESP_AUDIO_TYPE_PCM;    // WAV 由 PCM 解码器处理
	if (eq(ext, "flac")) return ESP_AUDIO_TYPE_FLAC;
	if (eq(ext, "aac"))  return ESP_AUDIO_TYPE_AAC;
	if (eq(ext, "m4a"))  return ESP_AUDIO_TYPE_AAC;
	if (eq(ext, "ogg"))  return ESP_AUDIO_TYPE_VORBIS;
	if (eq(ext, "opus")) return ESP_AUDIO_TYPE_OPUS;
	if (eq(ext, "pcm"))  return ESP_AUDIO_TYPE_PCM;
	if (eq(ext, "raw"))  return ESP_AUDIO_TYPE_PCM;
	if (eq(ext, "wma"))  return ESP_AUDIO_TYPE_UNSUPPORT;

	// 默认当作 PCM
	return ESP_AUDIO_TYPE_PCM;
}
