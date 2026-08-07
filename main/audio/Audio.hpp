#pragma once

#include <memory>
#include <atomic>
#include <functional>

#include "esp_audio_render.h"
#include "esp_audio_render_types.h"
#include "esp_audio_dec.h"
#include "esp_gmf_pool.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mutex/mutex.hpp"
#include "storage/fat.hpp"

class ES8311;
class Audio;      // forward declaration for AudioHandle::Impl
class AudioHandle; // forward declaration for AudioSample::play()

// ──────────────────────────────────────────────
//  AudioSample — 预解码音频采样（缓存）
//
//  将音频文件预解码为 PCM 存入 PSRAM，播放时绕过
//  SD 卡 I/O 和 AAC/MP3 解码，大幅降低延迟。
// ──────────────────────────────────────────────
class AudioSample
{
public:
	AudioSample() = default;
	~AudioSample() { unload(); }
	AudioSample(AudioSample&& other) noexcept
		: m_data(other.m_data), m_frames(other.m_frames),
		  m_sampleRate(other.m_sampleRate), m_channels(other.m_channels)
	{
		other.m_data = nullptr;
		other.m_frames = 0;
	}
	AudioSample& operator=(AudioSample&& other) noexcept
	{
		if (this != &other) {
			unload();
			m_data = other.m_data;       other.m_data = nullptr;
			m_frames = other.m_frames;    other.m_frames = 0;
			m_sampleRate = other.m_sampleRate;
			m_channels = other.m_channels;
		}
		return *this;
	}

	/** 从文件解码到 PCM（可多次调用，自动替换） */
	bool load(const char* path);
	/** 释放 PCM 内存 */
	void unload();

	/** 从缓存播放（不读 SD、不解码，直接流式 PCM） */
	AudioHandle play(bool loop = false);

	/** 当前正在播放的实例数（用于应用层限流） */
	int activeCount() const { return m_activeCount.load(); }
	/** 内部：播放结束回调，递减 activeCount */
	void _releaseInstance() { m_activeCount.fetch_sub(1); }

	bool isValid() const { return m_data != nullptr; }

private:
	int16_t*  m_data{};
	uint32_t  m_frames{};      // 总帧数（每声道）
	uint32_t  m_sampleRate{};
	uint8_t   m_channels{};
	mutable std::atomic<int> m_activeCount{0};

	friend class Audio;
};

// ──────────────────────────────────────────────
//  AudioHandle — 音频播放句柄（值类型）
//
//  效仿 std::thread 的生命周期模型：
//    - 默认绑定：handle 析构时停止播放
//    - detach()：解绑，handle 析构不影响播放
// ──────────────────────────────────────────────
class AudioHandle
{
public:
	AudioHandle() = default;
	~AudioHandle();

	AudioHandle(const AudioHandle&) = default;
	AudioHandle& operator=(const AudioHandle&) = default;

	AudioHandle(AudioHandle&& move);
	AudioHandle& operator=(AudioHandle&& move);

	/** 设置循环播放 */
	AudioHandle& setLoop(bool loop);
	/** 设置音量 0.0 ~ 1.0（运行时调用实时生效） */
	AudioHandle& setVolume(float volume);
	/** 获取当前音量 0.0 ~ 1.0 */
	float getVolume() const;
	/** 显式开始播放 */
	void play();
	/** 立即停止播放并清理资源 */
	void stop();
	/** 解绑生命周期，自动 play()，handle 析构不影响播放 */
	void detach();
	/** 暂停播放（解码任务暂停，流不关闭，ES8311 保持打开） */
	void pause();
	/** 恢复播放（从断点继续解码） */
	void resume();
	/** 是否处于暂停状态 */
	bool isPaused() const { return impl && impl->pauseRequested.load(); }

	bool isPlaying() const { return impl && impl->running.load(); }
	explicit operator bool() const { return impl != nullptr; }

private:
	struct Impl
	{
		std::shared_ptr<Impl> taskRef{};   // 任务持有，防止 detach 后提前析构

		Audio*            manager{};
		esp_audio_render_stream_handle_t stream{};
		esp_audio_dec_handle_t           dec{};
		IFile                            file{};
		TaskHandle_t                     task{};

		// PCM buffer mode（!= nullptr 时启用，绕过 file+decoder）
		const int16_t*  pcmBuffer{};
		AudioSample* sampleRef{};  // 用于递减 activeCount
		size_t          pcmTotalFrames{};
		size_t          pcmReadPos{};
		uint32_t        pcmSampleRate{};
		uint8_t         pcmChannels{};

		uint8_t* encBuf{};
		uint8_t* pcmBuf{};
		size_t   pcmBufSize{};

		esp_audio_render_sample_info_t inputInfo{};
		esp_audio_render_stream_id_t   streamId{};

		std::atomic<bool> running{false};
		std::atomic<bool> stopRequested{false};
		std::atomic<bool> pauseRequested{false};
		int64_t m_batchTick{};  // 自适应限速时间戳

		std::atomic<float> volume{1.0f};
		bool  loop{false};
		bool  detached{false};
		bool  started{false};
		bool  streamClosedByPause{false};

		static constexpr size_t EncBufSize = 8192;
		static constexpr size_t PcmBufSize = 16384;

		Impl()  = default;
		~Impl();

		static void taskFunc(void* arg);
		void        taskLoop();

		void applyVolume(int16_t* samples, size_t count, float vol);
	};

	std::shared_ptr<Impl> impl;

	AudioHandle(std::shared_ptr<Impl> p) : impl(std::move(p)) {}
	friend class Audio;
};

// ──────────────────────────────────────────────
//  Audio — 音频管理器（Meyer's Singleton）
// ──────────────────────────────────────────────
class Audio
{
public:
	static Audio& instance();

	/** 初始化音频子系统
	 *  @param codec  已 init 的 ES8311 引用
	 */
	bool init(ES8311& codec);
	void deinit();

	/** 主音量控制（委托到 ES8311 硬件编解码器） */
	static void setMasterVolume(int percent);
	static int getMasterVolume();
	static void saveVolumeToNvs();
	static void loadVolumeFromNvs();

	/** 从文件路径播放 */
	static AudioHandle play(const char* path);
	/** 从预解码缓存播放 */
	static AudioHandle play(const AudioSample& sample, bool loop = false);

	/** 当前已分配的音频流数（应用层用于限流，避免超出 MaxStreamNum） */
	static int usedStreamCount();

	/** 检测文件格式 */
	static esp_audio_type_t detectFormat(const char* path);

private:
	Audio()  = default;
	~Audio() { deinit(); }
	Audio(const Audio&) = delete;
	Audio& operator=(const Audio&) = delete;

	static int outWriter(uint8_t* pcm, uint32_t size, void* ctx);
	/** Render 事件回调：首个流 open → ES8311::open()，末个流 close → ES8311::close() */
	static int onRenderEvent(esp_audio_render_event_type_t event, void* ctx);

	esp_audio_render_handle_t render{};
	esp_gmf_pool_handle_t     pool{};
	ES8311*                   codec{};
	bool                      initialized{};
	int                       m_volume{100};  // 当前主音量（OPENED 时恢复）
	Mutex                     m_codecMutex{}; // 保护 codec->open/close 并发

	uint16_t streamUsedMask{};  // 流分配位图，bit n = stream n 已分配

	/** 分配一个空闲的 stream ID，返回 0xFF 表示无空闲 */
	esp_audio_render_stream_id_t allocStreamId();
	/** 释放 stream ID */
	void freeStreamId(esp_audio_render_stream_id_t id);

	static constexpr uint8_t MaxStreamNum = 14;
	static constexpr size_t  TaskStackSize = 4096;

	friend class AudioHandle;
	friend struct AudioHandle::Impl;
};
