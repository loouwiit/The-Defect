#include "audio/audioInput.hpp"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstring>

#include "audio/Audio.hpp"
#include "audio/wavFile.hpp"
#include "storage/mem.hpp"
#include "task/task.hpp"

static constexpr char TAG[] = "AudioInput";

// ══════════════════════════════════════════════
//  单例 / 初始化
// ══════════════════════════════════════════════

AudioInput& AudioInput::instance()
{
	static AudioInput inst;
	return inst;
}

bool AudioInput::init(ES8311* codec)
{
	if (codec == nullptr) {
		ESP_LOGE(TAG, "codec 为空");
		return false;
	}
	if (!codec->isReady()) {
		ESP_LOGE(TAG, "编解码器未初始化");
		return false;
	}
	if (!codec->hasInput()) {
		ESP_LOGE(TAG, "编解码器无录音能力（Config::i2s_din 未配置）");
		return false;
	}

	// 默认路径落在 memFS（PSRAM）：未挂载时会静默落回 FAT → 录音磨损 flash
	if (strncmp(DefaultPath, "/root/mem", 9) == 0 && !memFsIsMounted()) {
		ESP_LOGE(TAG, "memFS 未挂载（/root/mem 目录不存在？），拒绝初始化以免录音写入 FAT");
		return false;
	}

	m_codec = codec;
	m_initialized = true;
	ESP_LOGI(TAG, "录音服务就绪: %lu Hz / %u bit / %u ch, 最长 %lu ms, 默认路径 %s",
		(unsigned long)DefaultSampleRate, DefaultBitsPerSample, DefaultChannels,
		(unsigned long)DefaultMaxMs, DefaultPath);
	return true;
}

// ══════════════════════════════════════════════
//  录音控制
// ══════════════════════════════════════════════

bool AudioInput::start(const char* path, uint32_t maxMs)
{
	if (!m_initialized || m_codec == nullptr) {
		ESP_LOGE(TAG, "录音服务未初始化");
		return false;
	}
	if (m_recording.load()) {
		ESP_LOGW(TAG, "已在录音中");
		return false;
	}
	if (maxMs == 0) {
		maxMs = DefaultMaxMs;
	}

	const char* outPath = (path != nullptr && path[0] != '\0') ? path : DefaultPath;
	if (strlen(outPath) >= sizeof(m_path)) {
		ESP_LOGE(TAG, "路径过长: %s", outPath);
		return false;
	}

	// 1) 静音 DAC 输出（全双工，不暂停解码；PA 由 ADC 实例的 pa_pin=-1 保护）
	Audio::setRecordMode(true);

	// 2) 打开采集流（失败回滚静音）
	if (!m_codec->openInput()) {
		ESP_LOGE(TAG, "打开录音流失败");
		Audio::setRecordMode(false);
		return false;
	}
	// 麦克风增益（需在 openInput 之后；ES8311 按 6dB 档位取整）
	m_codec->setMicGain(m_micGainDb);
	if (m_adcVolReg >= 0) {
		// ES8311 ADC 数字音量 REG17（驱动默认 0xBF）；诊断用覆盖，下次 start 重置
		m_codec->setInputReg(0x17, m_adcVolReg);
	}

	// 3) 创建文件（⚠ OFile 无 O_TRUNC → 必须先删，否则旧长度残留）
	removeFile(outPath);
	if (!m_file.open(outPath)) {
		ESP_LOGE(TAG, "无法创建 %s", outPath);
		m_codec->closeInput();
		Audio::setRecordMode(false);
		return false;
	}

	strncpy(m_path, outPath, sizeof(m_path) - 1);
	m_path[sizeof(m_path) - 1] = '\0';
	m_maxMs = maxMs;

	// 4) 先写占位 WAV 头（停止时回填长度）
	if (!Wav::writeHeader(m_file, DefaultSampleRate, DefaultChannels,
	                      DefaultBitsPerSample, 0)) {
		ESP_LOGE(TAG, "写入 WAV 头失败");
		m_file.close();
		removeFile(m_path);
		m_codec->closeInput();
		Audio::setRecordMode(false);
		return false;
	}

	// 5) 重置状态并启动采集任务
	m_dataBytes.store(0);
	m_lastBytes.store(0);
	m_level.store(0);
	m_peak.store(0);
	m_peakR.store(0);
	m_dc.store(0);
	m_rms.store(0);
	m_clipped.store(0);
	m_frames.store(0);
	m_lastOk.store(false);
	m_stopRequested.store(false);
	m_done.store(false);
	m_recording.store(true);

	// 采集任务栈在 PSRAM 即可：本任务只做 I2S 读 + memFS 写，不触碰 flash
	if (xTaskCreate(taskEntry, "audioIn", 4096, this, Task::Priority::High, &m_task) != pdPASS) {
		ESP_LOGE(TAG, "创建采集任务失败");
		m_recording.store(false);
		m_done.store(true);
		m_file.close();
		removeFile(m_path);
		m_codec->closeInput();
		Audio::setRecordMode(false);
		return false;
	}

	ESP_LOGI(TAG, "开始录音: %s (最长 %lu ms)", m_path, (unsigned long)m_maxMs);
	return true;
}

AudioInput::Result AudioInput::stop()
{
	Result r{};

	if (!m_initialized || !m_recording.load()) {
		return r;
	}

	// 通知采集任务退出（当前分块最多阻塞 ~85ms + I2S 读超时余量）
	m_stopRequested.store(true);
	for (int i = 0; i < 200 && !m_done.load(); ++i) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (!m_done.load()) {
		ESP_LOGW(TAG, "采集任务未在 2s 内退出，文件可能仍在写入");
	}
	m_task = nullptr;   // 任务自删，句柄已失效（仅避免悬挂句柄残留）

	m_recording.store(false);
	m_codec->closeInput();
	Audio::setRecordMode(false);
	m_level.store(0);

	r.ok = m_lastOk.load();
	r.bytes = m_lastBytes.load();
	r.ms = r.bytes / BytesPerMs;
	r.path = m_path;

	ESP_LOGI(TAG, "录音结束: ok=%d, %lu 字节, %lu ms, %s",
		(int)r.ok, (unsigned long)r.bytes, (unsigned long)r.ms, r.path);
	return r;
}

void AudioInput::cancel()
{
	Result r = stop();
	if (r.path != nullptr) {
		removeFile(r.path);
		ESP_LOGI(TAG, "已放弃录音并删除 %s", r.path);
	}
}

AudioInput::Stats AudioInput::stats() const
{
	Stats s{};
	s.peakLeft  = m_peak.load();
	s.peakRight = m_peakR.load();
	s.dcLeft    = m_dc.load();
	s.rmsLeft   = m_rms.load();
	s.clipped   = m_clipped.load();
	s.frames    = m_frames.load();
	return s;
}

// ══════════════════════════════════════════════
//  采集任务
// ══════════════════════════════════════════════

void AudioInput::taskEntry(void* param)
{
	auto* self = static_cast<AudioInput*>(param);
	self->captureLoop();
	self->m_done.store(true);
	vTaskDelete(nullptr);
}

void AudioInput::captureLoop()
{
	// 采集以「立体声」进行（与 TX 槽配置一致，见 ES8311::openInput 注释），
	// 循环内取左声道降混为单声道后写文件 → 数据率 96KB/s（48k × 16bit × 1ch）。
	const uint32_t maxMonoBytes = m_maxMs * BytesPerMs;
	uint32_t written = 0;             // 已写入的**单声道**字节数
	bool ok = false;

	uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(ChunkBytes * 2, MALLOC_CAP_SPIRAM));
	if (buf == nullptr) {
		ESP_LOGE(TAG, "分配采集缓冲失败");
	}
	else {
		// 统计量（左声道 = 麦克风；右声道仅用于诊断声道映射）
		int      peakL = 0, peakR = 0;
		int64_t  sumL = 0;
		uint64_t sumSqL = 0;
		uint32_t clipped = 0, frames = 0;

		ok = true;
		while (!m_stopRequested.load() && written < maxMonoBytes) {
			// 目标单声道字节数（按帧对齐：每帧 2ch × 2B = 4B）
			uint32_t wantMono = ChunkBytes;
			if (wantMono > maxMonoBytes - written) {
				wantMono = maxMonoBytes - written;
			}
			wantMono &= ~1u;   // 保证是完整帧（避免半帧）

			const size_t wantStereo = wantMono * 2;
			if (!m_codec->read(buf, (int)wantStereo)) {
				ESP_LOGE(TAG, "I2S 读取失败，提前结束");
				ok = false;
				break;
			}

			// 左声道降混：mono[i] = stereo[2i]（就地，写入偏移始终落后于读取偏移）
			int16_t* samples = reinterpret_cast<int16_t*>(buf);
			const size_t frameCount = wantMono / sizeof(int16_t);
			int chunkPeak = 0;
			for (size_t i = 0; i < frameCount; ++i) {
				int16_t l = samples[i * 2];
				int16_t r = samples[i * 2 + 1];
				samples[i] = l;

				int al = l < 0 ? -l : l;
				int ar = r < 0 ? -r : r;
				if (al > peakL) peakL = al;
				if (ar > peakR) peakR = ar;
				if (al > chunkPeak) chunkPeak = al;
				if (al >= 32768 || ar >= 32768) ++clipped;
				sumL += l;
				sumSqL += (uint64_t)((int32_t)l * l);
			}
			frames += (uint32_t)frameCount;

			// 峰值电平（50% 满量程计为 100，便于观察人声）
			int level = chunkPeak * 100 / 16384;
			m_level.store(level > 100 ? 100 : level);

			if (m_file.write(buf, wantMono) != wantMono) {
				ESP_LOGE(TAG, "写入 WAV 失败（%u 字节）", (unsigned)wantMono);
				ok = false;
				break;
			}

			written += wantMono;
			m_dataBytes.store(written);
		}

		// 发布统计（仅采集线程写；停止后由 stats() 读取）
		m_peak.store(peakL);
		m_peakR.store(peakR);
		m_frames.store(frames);
		m_clipped.store(clipped);
		if (frames > 0) {
			m_dc.store((int)(sumL / (int64_t)frames));
			// 整数平方根近似足够（避免引入 math 依赖）
			uint64_t mean = sumSqL / frames;
			uint32_t r = 0;
			while ((uint64_t)(r + 1) * (r + 1) <= mean) ++r;
			m_rms.store((int)r);
		}

		heap_caps_free(buf);
	}

	// 回填 RIFF/data 长度（文件仍在打开状态）
	if (!Wav::updateSizes(m_file, written)) {
		ESP_LOGW(TAG, "回填 WAV 长度失败");
		ok = false;
	}
	m_file.close();

	m_lastBytes.store(written);
	m_lastOk.store(ok && written > 0);

	if (m_stopRequested.load()) {
		ESP_LOGI(TAG, "采集任务结束（用户停止）: %lu 字节", (unsigned long)written);
	}
	else if (written >= maxMonoBytes) {
		ESP_LOGI(TAG, "采集任务结束（达到时长上限）: %lu 字节", (unsigned long)written);
	}
}
