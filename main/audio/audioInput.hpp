#pragma once

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio/ES8311.hpp"
#include "storage/fat.hpp"

/**
 * AudioInput — 麦克风录音服务（单例）
 *
 * 采集链路：ES8311 ADC → I2S RX → 采集 Task → WAV 直写 memFS。
 *
 * 关键设计：
 * - **固定 48kHz / 16bit**：与播放链路（render 输出 48000/16/2）一致 → **零时钟重配**。
 *   ⚠ I2S 侧按**立体声**读取（RX 槽配置必须与 TX 相同，否则全双工下帧长错位），
 *   循环内取**左声道**（ES8311 麦克风数据）降混为单声道后写入 → 文件为 48kHz/16bit/mono。
 * - **写 `/root/mem`（memFS / PSRAM）**：不占 flash I/O，不需要内部 RAM 栈，
 *   固定文件名 + 即用即删，无需清理策略。
 * - **全双工静音**：录音期间不暂停解码，仅 `Audio::setRecordMode(true)`
 *   静音 DAC 输出（避免回声），停止后恢复。
 * - PA 不受影响：ES8311 的 ADC 实例以 `pa_pin = -1` 创建，不会碰功放引脚。
 *
 * 使用：
 * @code{cpp}
 *   AudioInput::instance().init(&audio);
 *   if (AudioInput::instance().start()) { ... }        // 开始（最长 10s）
 *   auto r = AudioInput::instance().stop();            // 停止并落盘
 *   if (r.ok) Audio::play(r.path).detach();
 * @endcode
 */
class AudioInput
{
public:
	/** 默认录音文件：memFS（PSRAM），固定名，即用即删 */
	static constexpr const char* DefaultPath = "/root/mem/voice.wav";
	static constexpr uint32_t DefaultSampleRate = 48000;
	static constexpr uint16_t DefaultBitsPerSample = 16;
	static constexpr uint16_t DefaultChannels = 1;
	static constexpr uint32_t DefaultMaxMs = 10000;   //!< 10s ≈ 0.92MB

	/** 每毫秒 PCM 字节数（48000 * 2 / 1000） */
	static constexpr uint32_t BytesPerMs =
		DefaultSampleRate * (DefaultBitsPerSample / 8) * DefaultChannels / 1000;

	struct Result
	{
		bool     ok{};      //!< 采集成功且有数据
		uint32_t ms{};      //!< 实际录音时长（毫秒）
		uint32_t bytes{};   //!< PCM 数据字节数（不含 44 字节头）
		const char* path{}; //!< WAV 文件路径
	};

	/** 采集统计（停止后有效，供自测/诊断） */
	struct Stats
	{
		int      peakLeft{};   //!< 左声道（麦克风）绝对峰值 0~32768
		int      peakRight{};  //!< 右声道绝对峰值（诊断声道映射）
		int      dcLeft{};     //!< 左声道均值（诊断：接近满量程 = 直流/悬空）
		int      rmsLeft{};    //!< 左声道 RMS
		uint32_t clipped{};    //!< 达到满量程的样本数（削顶计数）
		uint32_t frames{};     //!< 样本帧数
	};

	static AudioInput& instance();

	/** 绑定编解码器（main.cpp 启动阶段调用一次） */
	bool init(ES8311* codec);

	/**
	 * @brief 开始录音
	 * @param path  输出 WAV 路径，nullptr 用 DefaultPath
	 * @param maxMs 最长录音时长（毫秒），到点自动停止
	 * @return false = 已在录音 / 未初始化 / 编解码器无录音能力 / 文件打开失败
	 */
	bool start(const char* path = nullptr, uint32_t maxMs = DefaultMaxMs);

	/** @brief 停止录音并回填 WAV 头（幂等；未在录音时返回 ok = false） */
	Result stop();

	/** @brief 停止录音并删除文件（放弃本次录音） */
	void cancel();

	bool isRecording() const { return m_recording.load(); }

	/** @brief 已录制时长（毫秒） */
	uint32_t elapsedMs() const { return m_dataBytes.load() / BytesPerMs; }

	/** @brief 峰值电平 0~100（供 UI 指示，最近一个分块） */
	int level() const { return m_level.load(); }

	/** @brief 本次录音的绝对峰值（0~32768，满量程；左声道 = 麦克风） */
	int peak() const { return m_peak.load(); }

	/** @brief 本次录音统计（停止后有效） */
	Stats stats() const;

	/** @brief 麦克风增益 dB（0~42，默认 30），下次 start() 生效 */
	void setMicGainDb(float db) { m_micGainDb = db; }

	/** @brief 覆盖 ES8311 ADC 数字音量寄存器 REG17（诊断用；<0 = 不覆盖，保持驱动默认 0xBF） */
	void setAdcVolumeReg(int reg) { m_adcVolReg = reg; }

	/** 单声道写盘分块字节数（≈85ms @48k/16bit/mono；I2S 读缓冲为其 2 倍） */
	static constexpr size_t ChunkBytes = 8 * 1024;

private:
	AudioInput() = default;
	~AudioInput() = default;
	AudioInput(const AudioInput&) = delete;
	AudioInput& operator=(const AudioInput&) = delete;

	static void taskEntry(void* param);
	void captureLoop();

	ES8311* m_codec{};
	bool m_initialized{};

	OFile m_file{};

	std::atomic<bool> m_recording{false};
	std::atomic<bool> m_stopRequested{false};
	std::atomic<bool> m_done{true};
	std::atomic<bool> m_lastOk{false};
	std::atomic<uint32_t> m_dataBytes{0};
	std::atomic<uint32_t> m_lastBytes{0};
	std::atomic<int> m_level{0};
	std::atomic<int> m_peak{0};    //!< 左声道绝对峰值
	std::atomic<int> m_peakR{0};   //!< 右声道绝对峰值
	std::atomic<int> m_dc{0};      //!< 左声道均值
	std::atomic<int> m_rms{0};     //!< 左声道 RMS
	std::atomic<uint32_t> m_clipped{0};
	std::atomic<uint32_t> m_frames{0};

	float m_micGainDb{ 30.0f };
	int   m_adcVolReg{ -1 };

	char m_path[64]{};
	uint32_t m_maxMs{DefaultMaxMs};
	TaskHandle_t m_task{};
};
