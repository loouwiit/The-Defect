#include "audio/ES8311.hpp"

#include <esp_log.h>
#include <driver/i2s_std.h>

// esp_codec_dev 接口头文件
#include "audio_codec_if.h"
#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_gpio_if.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"

static constexpr char TAG[] = "ES8311";

// ────────────────────────────────────────────────────────────
// 构造 / 析构
// ────────────────────────────────────────────────────────────

ES8311::~ES8311()
{
	deinit();
}

// ────────────────────────────────────────────────────────────
// 初始化
// ────────────────────────────────────────────────────────────

bool ES8311::init(IIC& iic, Config config)
{
	if (initialized) {
		ESP_LOGW(TAG, "已经初始化");
		return true;
	}

	cfg = config;

	// ── 1. 安装 I2S 通道（双工：TX 播放 + RX 录音） ─────────
	// ES8311 只有一条 I2S 总线（MCLK/BCK/WS 收发共用），故 RX/TX 必须挂在
	// 同一个 I2S 控制器上（pair channel 共享时钟），不能开第二个控制器。
	const bool duplex = (cfg.i2s_din != GPIO_NUM_NC);
	ESP_LOGI(TAG, "安装 I2S 通道（%s）", duplex ? "双工 TX+RX" : "仅 TX");

	i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
	ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, (i2s_chan_handle_t*)&txHandle,
	                                duplex ? (i2s_chan_handle_t*)&rxHandle : nullptr));

	// Philips 格式，16bit，立体声（实际采样率由 esp_codec_dev_open 按格式重配）
	i2s_std_config_t stdCfg = {
		.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
		.gpio_cfg = {
			.mclk = cfg.i2s_mck,
			.bclk = cfg.i2s_bck,
			.ws   = cfg.i2s_ws,
			.dout = cfg.i2s_dout,
			.din  = cfg.i2s_din,
		},
	};

	ESP_ERROR_CHECK(i2s_channel_init_std_mode((i2s_chan_handle_t)txHandle, &stdCfg));
	if (rxHandle)
		ESP_ERROR_CHECK(i2s_channel_init_std_mode((i2s_chan_handle_t)rxHandle, &stdCfg));

	// ── 2. 创建 Codec 接口 ────────────────────────────────
	ESP_LOGI(TAG, "创建 Codec 接口");

	// 后续任一创建失败必须清理已创建的 I2S 通道
	auto failCleanup = [&](const char* what) -> bool {
		ESP_LOGE(TAG, "%s", what);
		if (rxHandle) {
			i2s_channel_disable((i2s_chan_handle_t)rxHandle);
			i2s_del_channel((i2s_chan_handle_t)rxHandle);
			rxHandle = nullptr;
		}
		if (txHandle) {
			i2s_channel_disable((i2s_chan_handle_t)txHandle);
			i2s_del_channel((i2s_chan_handle_t)txHandle);
			txHandle = nullptr;
		}
		return false;
	};

	// I2C 控制接口：复用已有的 I2C 总线
	audio_codec_i2c_cfg_t i2cCfg = {
		.port      = 0,
		.addr      = cfg.codec_addr,
		.bus_handle = iic.getBusHandle(),
	};
	const audio_codec_ctrl_if_t* ctrlIf = audio_codec_new_i2c_ctrl(&i2cCfg);
	if (ctrlIf == nullptr) {
		return failCleanup("创建 I2C 控制接口失败");
	}

	// GPIO 接口
	const audio_codec_gpio_if_t* gpioIf = audio_codec_new_gpio();
	if (gpioIf == nullptr) {
		return failCleanup("创建 GPIO 接口失败");
	}

	// ES8311 编解码器
	es8311_codec_cfg_t es8311Cfg = {
		.ctrl_if     = ctrlIf,
		.gpio_if     = gpioIf,
		.codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,  // 仅播放
		.pa_pin      = (gpio_num_t)cfg.pa_pin,
		.pa_reverted = cfg.pa_reverted,
		.master_mode = false,
		.use_mclk    = cfg.use_mclk,
		.digital_mic = false,
	};
	const audio_codec_if_t* codecIf = es8311_codec_new(&es8311Cfg);
	if (codecIf == nullptr) {
		return failCleanup("创建 ES8311 Codec 接口失败");
	}

	// I2S 数据接口（同一接口同时带 TX+RX → 驱动内部自配对，共享时钟）
	audio_codec_i2s_cfg_t i2sDataCfg = {
		.port      = 0,
		.rx_handle = rxHandle,
		.tx_handle = txHandle,
	};
	const audio_codec_data_if_t* dataIf = audio_codec_new_i2s_data(&i2sDataCfg);
	if (dataIf == nullptr) {
		return failCleanup("创建 I2S 数据接口失败");
	}

	// ── 3. 创建 Codec 设备句柄（播放 OUT / 录音 IN） ────────
	esp_codec_dev_cfg_t devCfg = {
		.dev_type = ESP_CODEC_DEV_TYPE_OUT,
		.codec_if = codecIf,
		.data_if  = dataIf,
	};
	codecDev = esp_codec_dev_new(&devCfg);
	if (codecDev == nullptr) {
		return failCleanup("创建 Codec 设备句柄失败");
	}

	if (rxHandle) {
		// ADC 实例：与 DAC 实例共用 ctrl_if / gpio_if / data_if。
		// ⚠ ctrl_if 必须复用——`i2c_master_bus_add_device` 对同一地址只允许添加一次，
		//   新建第二个 ctrl_if 会返回 ESP_ERR_INVALID_STATE（构造直接失败）。
		// ES8311 驱动内部用 paired_8311(adc/dac) 机制管理两实例共存，一侧仍启用时
		// suspend 自动跳过，故 DAC 播放与 ADC 录音可全双工。
		es8311_codec_cfg_t adcCfg = es8311Cfg;
		adcCfg.codec_mode  = ESP_CODEC_DEV_WORK_MODE_ADC;
		adcCfg.digital_mic = cfg.digital_mic;
		adcCfg.no_dac_ref  = cfg.no_dac_ref;
		// ⚠ pa_pin = -1：让 ADC 实例完全不碰 PA 引脚。
		//   es8311_open() 会无条件执行 es8311_pa_power(SETUP|DISABLE) 关闭功放，
		//   而 ADC 实例的 es8311_start() 不经过 DAC 分支、不会重新使能 →
		//   若共用 pa_pin，录音（重）打开后功放将永久静音。传 -1 可彻底规避。
		adcCfg.pa_pin = -1;

		const audio_codec_if_t* adcCodecIf = es8311_codec_new(&adcCfg);
		if (adcCodecIf == nullptr) {
			return failCleanup("创建 ES8311 ADC Codec 接口失败");
		}

		esp_codec_dev_cfg_t inDevCfg = {
			.dev_type = ESP_CODEC_DEV_TYPE_IN,
			.codec_if = adcCodecIf,
			.data_if  = dataIf,
		};
		inputCodecDev = esp_codec_dev_new(&inDevCfg);
		if (inputCodecDev == nullptr) {
			return failCleanup("创建录音 Codec 设备句柄失败");
		}
	}

	initialized = true;
	ESP_LOGI(TAG, "音频初始化完成");
	return true;
}

// ────────────────────────────────────────────────────────────
// 反初始化
// ────────────────────────────────────────────────────────────

bool ES8311::deinit()
{
	if (!initialized) {
		return true;
	}
	initialized = false;

	// 先关闭流（如果仍在流式/录音状态）
	closeInput();
	close();

	// 关闭 I2S 通道
	if (rxHandle) {
		i2s_channel_disable((i2s_chan_handle_t)rxHandle);
		i2s_del_channel((i2s_chan_handle_t)rxHandle);
		rxHandle = nullptr;
	}
	if (txHandle) {
		i2s_channel_disable((i2s_chan_handle_t)txHandle);
		i2s_del_channel((i2s_chan_handle_t)txHandle);
		txHandle = nullptr;
	}

	ESP_LOGI(TAG, "音频已关闭");
	return true;
}

// ────────────────────────────────────────────────────────────
// 流式播放接口
// ────────────────────────────────────────────────────────────

static esp_codec_dev_sample_info_t defaultSampleInfo()
{
	return {
		.bits_per_sample = 16,
		.channel         = 2,
		.channel_mask    = 0,
		.sample_rate     = 44100,
		.mclk_multiple   = 0,
	};
}

bool ES8311::open(esp_codec_dev_sample_info_t* fs)
{
	if (!initialized || codecDev == nullptr) {
		ESP_LOGE(TAG, "音频未初始化");
		return false;
	}
	if (streaming) {
		ESP_LOGW(TAG, "已在流式状态，先 close");
		return false;
	}

	esp_codec_dev_sample_info_t defaultFs = defaultSampleInfo();
	if (fs == nullptr) {
		fs = &defaultFs;
	}

	int ret = esp_codec_dev_open(codecDev, fs);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "打开音频设备失败: %d", ret);
		return false;
	}

	streaming = true;
	return true;
}

bool ES8311::write(const void* data, int len)
{
	if (!streaming || codecDev == nullptr) {
		ESP_LOGE(TAG, "未处于流式状态，请先调用 open");
		return false;
	}

	int ret = esp_codec_dev_write(codecDev, (void*)data, len);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "写入失败: %d", ret);
		return false;
	}
	return true;
}

bool ES8311::close()
{
	if (!streaming) {
		return true;
	}
	streaming = false;

	esp_codec_dev_close(codecDev);
	return true;
}

// ────────────────────────────────────────────────────────────
// 一次性播放
// ────────────────────────────────────────────────────────────

bool ES8311::play(const void* data, int len, esp_codec_dev_sample_info_t* fs)
{
	if (!open(fs)) {
		return false;
	}
	bool ok = write(data, len);
	close();
	return ok;
}

// ────────────────────────────────────────────────────────────
// 音量控制
// ────────────────────────────────────────────────────────────

bool ES8311::setVolume(int volume)
{
	if (!initialized || codecDev == nullptr) {
		return false;
	}
	return esp_codec_dev_set_out_vol(codecDev, volume) == ESP_CODEC_DEV_OK;
}

int ES8311::getVolume()
{
	if (!initialized || codecDev == nullptr) {
		return 0;
	}
	int vol = 0;
	esp_codec_dev_get_out_vol(codecDev, &vol);
	return vol;
}

bool ES8311::setMute(bool mute)
{
	if (!initialized || codecDev == nullptr) {
		return false;
	}
	return esp_codec_dev_set_out_mute(codecDev, mute) == ESP_CODEC_DEV_OK;
}

// ────────────────────────────────────────────────────────────
// 录音接口（ADC）
// ────────────────────────────────────────────────────────────

static esp_codec_dev_sample_info_t defaultInputSampleInfo()
{
	// ⚠ 必须按 **立体声** 打开（channel=2 + channel_mask=0）：
	//   - 若请求单声道，驱动会把 fs 展开为「channel=2 + 左声道掩码」→
	//     `get_active_channel()` 返回 1 → RX 槽被配成 I2S_SLOT_MODE_MONO（WS 周期 = 16bit），
	//     而播放侧 TX 是 STEREO（WS 周期 = 32bit）→ 全双工下收发帧长不一致、采样错位。
	//   - 保持一致后 RX/TX 槽配置完全相同，零时钟/槽重配，全双工互不干扰。
	//   左声道为麦克风数据，右声道为 DAC 参考（或空），由调用方取左声道降混。
	return {
		.bits_per_sample = 16,
		.channel         = 2,       // 立体声读取，软件取左声道
		.channel_mask    = 0,       // 0 = 两槽全收（与 TX 一致）
		.sample_rate     = 48000,   // 与播放链路一致 → 不重配 I2S 时钟
		.mclk_multiple   = 0,
	};
}

bool ES8311::openInput(esp_codec_dev_sample_info_t* fs)
{
	if (!initialized || inputCodecDev == nullptr) {
		ESP_LOGE(TAG, "录音未初始化（Config::i2s_din 未配置？）");
		return false;
	}
	if (inputOpened) {
		ESP_LOGW(TAG, "已处于录音状态");
		return true;
	}

	esp_codec_dev_sample_info_t defaultFs = defaultInputSampleInfo();
	if (fs == nullptr) {
		fs = &defaultFs;
	}

	int ret = esp_codec_dev_open(inputCodecDev, fs);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "打开录音设备失败: %d", ret);
		return false;
	}

	inputOpened = true;
	ESP_LOGI(TAG, "录音已开始 (%lu Hz, %u bit, %u ch；左声道为麦克风)",
		(unsigned long)fs->sample_rate, fs->bits_per_sample, fs->channel);
	return true;
}

bool ES8311::read(void* data, int len)
{
	if (!inputOpened || inputCodecDev == nullptr) {
		ESP_LOGE(TAG, "未处于录音状态，请先调用 openInput");
		return false;
	}

	int ret = esp_codec_dev_read(inputCodecDev, data, len);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "录音读取失败: %d", ret);
		return false;
	}
	return true;
}

bool ES8311::closeInput()
{
	if (!inputOpened) {
		return true;
	}
	inputOpened = false;

	esp_codec_dev_close(inputCodecDev);
	ESP_LOGI(TAG, "录音已结束");
	return true;
}

bool ES8311::setMicGain(float db)
{
	if (inputCodecDev == nullptr) {
		return false;
	}
	// ⚠ 需在 openInput() 之后调用（编解码器未打开时驱动返回 WRONG_STATE）
	return esp_codec_dev_set_in_gain(inputCodecDev, db) == ESP_CODEC_DEV_OK;
}

bool ES8311::setMicMute(bool mute)
{
	if (inputCodecDev == nullptr) {
		return false;
	}
	return esp_codec_dev_set_in_mute(inputCodecDev, mute) == ESP_CODEC_DEV_OK;
}

bool ES8311::setInputReg(int reg, int value)
{
	if (inputCodecDev == nullptr) {
		return false;
	}
	return esp_codec_dev_write_reg(inputCodecDev, reg, value) == ESP_CODEC_DEV_OK;
}
