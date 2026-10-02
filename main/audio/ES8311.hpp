#pragma once

#include "iic/iic.hpp"
#include "gpio/gpio.hpp"
#include "esp_codec_dev_types.h"

/**
 * ES8311
 *
 * 封装 esp_codec_dev + ES8311 编解码器，提供简洁的播放 API。
 *
 * 使用方式：
 * @code{cpp}
 *   IIC iic{ {GPIO_NUM_xx}, {GPIO_NUM_xx} };  // I2C 总线
 *
 *   ES8311::Config cfg{
 *       .i2s_mck  = GPIO_NUM_xx,   // MCLK
 *       .i2s_bck  = GPIO_NUM_xx,   // BCK
 *       .i2s_ws   = GPIO_NUM_xx,   // WS/LRCK
 *       .i2s_dout = GPIO_NUM_xx,   // DATA/DSDIN（播放）
 *       .i2s_din  = GPIO_NUM_xx,   // DATA/ASDOUT（录音，可留空 = 仅播放）
 *       .pa_pin   = GPIO_NUM_xx,   // 功放使能
 *   };
 *
 *   ES8311 audio;
 *   audio.init(iic, cfg);
 *
 *   // 播放 PCM 数据
 *   audio.play(pcmData, dataSize);
 *
 *   // 录制 PCM 数据（需 i2s_din 有效）
 *   audio.openInput();                       // 默认 48kHz/16bit/单声道
 *   audio.read(buf, sizeof(buf));
 *   audio.closeInput();
 *
 *   // 调节音量 (0-100)
 *   audio.setVolume(60);
 * @endcode
 */
class ES8311
{
public:
	/** 音频硬件配置（引脚由外部传入） */
	struct Config
	{
		GPIO i2s_mck  = GPIO::NC;   //!< I2S MCLK（主时钟）
		GPIO i2s_bck  = GPIO::NC;   //!< I2S BCK（位时钟）
		GPIO i2s_ws   = GPIO::NC;   //!< I2S WS（声道选择/帧时钟）
		GPIO i2s_dout = GPIO::NC;   //!< I2S DATA/DSDIN（播放数据输出，P4 → 编解码器）
		GPIO i2s_din  = GPIO::NC;   //!< I2S DATA/ASDOUT（录音数据输入，编解码器 → P4）
		GPIO pa_pin   = GPIO::NC;   //!< PA（功放）使能引脚
		bool  pa_reverted = false;   //!< PA 使能电平是否反转
		bool  use_mclk     = true;  //!< 是否使用外部 MCLK
		uint8_t codec_addr = 0x30;  //!< 编解码器 I2C 地址（ES8311 默认 0x30 >> 1 = 0x18）
		bool  digital_mic  = false;  //!< 是否使用数字麦克风（PDM）；false = 模拟麦克风
		bool  no_dac_ref   = false;  //!< 录音双声道时右声道是否留空（false = 填 DAC 输出）
	};

	ES8311() = default;
	~ES8311();

	ES8311(const ES8311&) = delete;
	ES8311& operator=(const ES8311&) = delete;

	/** 初始化音频子系统 */
	bool init(IIC& iic, const Config config);

	/** 反初始化，释放资源 */
	bool deinit();

	/** 是否已初始化 */
	bool isReady() const { return initialized; }

	// ─── 播放控制 ───────────────────────────────────────

	/**
	 * @brief 一次性播放（内部 open → write → close）
	 * @param data PCM 数据
	 * @param len  数据长度（字节）
	 * @param fs   音频采样格式，nullptr 时默认 44.1kHz/16bit/立体声
	 */
	bool play(const void* data, int len, esp_codec_dev_sample_info_t* fs = nullptr);

	/**
	 * @brief 打开音频流（流式播放前调用）
	 * @param fs 音频采样格式，nullptr 时默认 44.1kHz/16bit/立体声
	 */
	bool open(esp_codec_dev_sample_info_t* fs = nullptr);

	/** @brief 写入一段 PCM 数据（需先调用 open） */
	bool write(const void* data, int len);

	/** @brief 关闭音频流 */
	bool close();

	/** @brief 是否正在流式播放中 */
	bool isStreaming() const { return streaming; }

	// ─── 音量控制 ───────────────────────────────────────

	/** 设置播放音量 0-100 */
	bool setVolume(int volume);

	/** 获取当前音量 0-100 */
	int  getVolume();

	/** 设置静音 */
	bool setMute(bool mute);

	// ─── 录音控制（需 Config::i2s_din 有效） ───────────────

	/**
	 * @brief 打开录音流（ADC）
	 * @param fs 采样格式，nullptr 时默认 48kHz/16bit/单声道
	 *           ⚠ 采集采样率应与播放保持一致（48000）以避免 I2S 时钟重配
	 */
	bool openInput(esp_codec_dev_sample_info_t* fs = nullptr);

	/** @brief 读取一段 PCM 数据（阻塞，需先 openInput） */
	bool read(void* data, int len);

	/** @brief 关闭录音流 */
	bool closeInput();

	/** @brief 是否处于录音状态 */
	bool isInputOpen() const { return inputOpened; }

	/** @brief 是否具备录音能力（Config::i2s_din 已配置并成功创建 ADC 实例） */
	bool hasInput() const { return inputCodecDev != nullptr; }

	/** @brief 设置麦克风增益（dB，ES8311 支持 0~42，按 6dB 档位取整） */
	bool setMicGain(float db);

	/** @brief 设置麦克风静音 */
	bool setMicMute(bool mute);

	/** @brief 直写录音侧（ADC 实例）寄存器（诊断/调参用，如 ADC 数字音量 0x17） */
	bool setInputReg(int reg, int value);

private:
	bool initialized{};
	bool streaming{};   //!< 是否处于 open → write → close 流式状态
	bool inputOpened{}; //!< 是否处于 openInput → read → closeInput 录音状态

	// esp_codec_dev 句柄
	void* codecDev{};      //!< 播放（DAC/OUT）
	void* inputCodecDev{}; //!< 录音（ADC/IN）

	// I2S 通道句柄
	void* txHandle{};
	void* rxHandle{};

	// 保存配置引用（仅 deinit 用）
	Config cfg{};
};
