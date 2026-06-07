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

	// ── 1. 安装 I2S TX 通道 ───────────────────────────────
	ESP_LOGI(TAG, "安装 I2S TX 通道");

	i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
	ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, (i2s_chan_handle_t*)&txHandle, nullptr));

	// Philips 格式，16bit，立体声
	i2s_std_config_t stdCfg = {
		.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
		.gpio_cfg = {
			.mclk = cfg.i2s_mck,
			.bclk = cfg.i2s_bck,
			.ws   = cfg.i2s_ws,
			.dout = cfg.i2s_dout,
			.din  = GPIO_NUM_NC,
		},
	};

	ESP_ERROR_CHECK(i2s_channel_init_std_mode((i2s_chan_handle_t)txHandle, &stdCfg));

	// ── 2. 创建 Codec 接口 ────────────────────────────────
	ESP_LOGI(TAG, "创建 Codec 接口");

	// I2C 控制接口：复用已有的 I2C 总线
	audio_codec_i2c_cfg_t i2cCfg = {
		.port      = 0,
		.addr      = cfg.codec_addr,
		.bus_handle = iic.getBusHandle(),
	};
	const audio_codec_ctrl_if_t* ctrlIf = audio_codec_new_i2c_ctrl(&i2cCfg);
	if (ctrlIf == nullptr) {
		ESP_LOGE(TAG, "创建 I2C 控制接口失败");
		return false;
	}

	// GPIO 接口
	const audio_codec_gpio_if_t* gpioIf = audio_codec_new_gpio();
	if (gpioIf == nullptr) {
		ESP_LOGE(TAG, "创建 GPIO 接口失败");
		return false;
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
		ESP_LOGE(TAG, "创建 ES8311 Codec 接口失败");
		return false;
	}

	// I2S 数据接口
	audio_codec_i2s_cfg_t i2sDataCfg = {
		.port      = 0,
		.rx_handle = nullptr,
		.tx_handle = txHandle,
	};
	const audio_codec_data_if_t* dataIf = audio_codec_new_i2s_data(&i2sDataCfg);
	if (dataIf == nullptr) {
		ESP_LOGE(TAG, "创建 I2S 数据接口失败");
		return false;
	}

	// ── 3. 创建 Codec 设备句柄 ────────────────────────────
	esp_codec_dev_cfg_t devCfg = {
		.dev_type = ESP_CODEC_DEV_TYPE_OUT,
		.codec_if = codecIf,
		.data_if  = dataIf,
	};
	codecDev = esp_codec_dev_new(&devCfg);
	if (codecDev == nullptr) {
		ESP_LOGE(TAG, "创建 Codec 设备句柄失败");
		return false;
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

	// 关闭 I2S 通道
	if (txHandle) {
		i2s_channel_disable((i2s_chan_handle_t)txHandle);
		i2s_del_channel((i2s_chan_handle_t)txHandle);
		txHandle = nullptr;
	}

	ESP_LOGI(TAG, "音频已关闭");
	return true;
}

// ────────────────────────────────────────────────────────────
// 播放
// ────────────────────────────────────────────────────────────

bool ES8311::play(const void* data, int len, esp_codec_dev_sample_info_t* fs)
{
	if (!initialized || codecDev == nullptr) {
		ESP_LOGE(TAG, "音频未初始化");
		return false;
	}

	// 使用传入格式，或默认 44.1kHz 16bit 立体声
	esp_codec_dev_sample_info_t defaultFs = {
		.bits_per_sample = 16,
		.channel         = 2,
		.channel_mask    = 0,
		.sample_rate     = 44100,
		.mclk_multiple   = 0,
	};
	if (fs == nullptr) {
		fs = &defaultFs;
	}

	int ret = esp_codec_dev_open(codecDev, fs);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "打开音频设备失败: %d", ret);
		return false;
	}

	ret = esp_codec_dev_write(codecDev, (void*)data, len);
	if (ret != ESP_CODEC_DEV_OK) {
		ESP_LOGE(TAG, "播放写入失败: %d", ret);
		esp_codec_dev_close(codecDev);
		return false;
	}

	esp_codec_dev_close(codecDev);
	return true;
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
