#include "audio/audioManager.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <strings.h>

#include "esp_log.h"
#include "esp_err.h"

// I2S
#include "driver/i2s_std.h"
#include "driver/i2s_common.h"

// ESP-Audio-Codec: decoders + simple decoder
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"

// ESP-Codec-Dev
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"

// ESP-Audio-Render
#include "esp_audio_render.h"

// ESP-GMF pool
#include "esp_gmf_pool.h"

// ESP-GMF-Audio elements (转换器，render 自动按需使用)
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr char TAG[] = "AudioManager";

// I2S 引脚配置
#define I2S_PORT    I2S_NUM_0
#define I2S_MCLK    GPIO_NUM_13
#define I2S_BCLK    GPIO_NUM_12
#define I2S_DOUT    GPIO_NUM_11
#define I2S_LRCK    GPIO_NUM_10
#define I2S_DIN     GPIO_NUM_9   // 预留录音

// ES8311 I2C 地址 (0x18 << 1 = 0x30)
#define ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR

// PA 使能引脚
#define PA_PIN      GPIO_NUM_53

// 输出采样参数
#define OUT_SAMPLE_RATE     48000
#define OUT_CHANNELS        2
#define OUT_BITS_PER_SAMPLE 16

// ============================================================================
// 工具函数
// ============================================================================

static esp_audio_simple_dec_type_t guessDecType(const char* uri)
{
    const char* ext = strrchr(uri, '.');
    if (!ext) return ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;

    // 转小写比较
    char lower[8] = {};
    for (int i = 0; ext[i] && i < 7; i++) {
        char c = ext[i];
        if (c >= 'A' && c <= 'Z') c += 0x20;
        lower[i] = c;
    }

    if      (strcmp(lower, ".wav")  == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    else if (strcmp(lower, ".mp3")  == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    else if (strcmp(lower, ".aac")  == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    else if (strcmp(lower, ".m4a")  == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    else if (strcmp(lower, ".flac") == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    else if (strcmp(lower, ".ogg")  == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
    else if (strcmp(lower, ".pcm")  == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;

    ESP_LOGW(TAG, "未知音频扩展名 %s，按 PCM 处理", ext);
    return ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;
}

// ============================================================================
// AudioManager 单例
// ============================================================================

AudioManager& AudioManager::instance()
{
    static AudioManager inst;
    return inst;
}

// ============================================================================
// init — I2S + ES8311 初始化
// ============================================================================

AudioManager& AudioManager::init(IIC& iic)
{
    ESP_LOGI(TAG, "初始化 I2S ...");

    // ---- 1. 安装 I2S 驱动 ----
    i2s_chan_handle_t txHandle = nullptr;
    i2s_chan_config_t chanCfg = {
        .id = I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear = true,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, &txHandle, nullptr));
    txHandle_ = txHandle;

    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(OUT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws   = I2S_LRCK,
            .dout = I2S_DOUT,
            .din  = I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(txHandle, &stdCfg));
    ESP_ERROR_CHECK(i2s_channel_enable(txHandle));

    // ---- 2. 创建码率设备接口 ----
    ESP_LOGI(TAG, "初始化 ES8311 ...");

    // I2S 数据接口
    audio_codec_i2s_cfg_t i2sCfg = {
        .port      = I2S_PORT,
        .rx_handle = nullptr,
        .tx_handle = txHandle,
        .clk_src   = 0,  // 默认时钟源
    };
    const audio_codec_data_if_t* dataIf = audio_codec_new_i2s_data(&i2sCfg);
    if (!dataIf) {
        ESP_LOGE(TAG, "audio_codec_new_i2s_data 失败");
        abort();
    }

    // I2C 控制接口（复用外部 IIC 总线）
    audio_codec_i2c_cfg_t i2cCfg = {
        .port       = 0,
        .addr       = ES8311_ADDR,
        .bus_handle = iic.getBusHandle(),
    };
    const audio_codec_ctrl_if_t* ctrlIf = audio_codec_new_i2c_ctrl(&i2cCfg);
    if (!ctrlIf) {
        ESP_LOGE(TAG, "audio_codec_new_i2c_ctrl 失败");
        abort();
    }

    // GPIO 接口
    const audio_codec_gpio_if_t* gpioIf = audio_codec_new_gpio();
    if (!gpioIf) {
        ESP_LOGE(TAG, "audio_codec_new_gpio 失败");
        abort();
    }

    // ES8311 编解码器接口
    es8311_codec_cfg_t es8311Cfg = {
        .ctrl_if     = ctrlIf,
        .gpio_if     = gpioIf,
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin      = PA_PIN,
        .pa_reverted = false,
        .master_mode = true,
        .use_mclk    = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain     = {
            .pa_voltage        = 5.0f,
            .codec_dac_voltage = 3.3f,
            .pa_gain           = 0.0f,
        },
        .no_dac_ref  = false,
        .mclk_div    = 0,
    };
    const audio_codec_if_t* codecIf = es8311_codec_new(&es8311Cfg);
    if (!codecIf) {
        ESP_LOGE(TAG, "es8311_codec_new 失败");
        abort();
    }

    // ---- 3. 创建 esp_codec_dev ----
    esp_codec_dev_cfg_t devCfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codecIf,
        .data_if  = dataIf,
    };
    esp_codec_dev_handle_t codecDev = esp_codec_dev_new(&devCfg);
    if (!codecDev) {
        ESP_LOGE(TAG, "esp_codec_dev_new 失败");
        abort();
    }
    codecDev_ = codecDev;

    // ---- 4. 打开编解码设备 ----
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = OUT_BITS_PER_SAMPLE,
        .channel         = OUT_CHANNELS,
        .channel_mask    = 0,
        .sample_rate     = OUT_SAMPLE_RATE,
        .mclk_multiple   = 0,
    };
    int ret = esp_codec_dev_open(codecDev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open 失败: %d", ret);
        abort();
    }

    // 初始音量设为 50%
    esp_codec_dev_set_out_vol(codecDev, 50);

    ESP_LOGI(TAG, "ES8311 初始化完成 (48000Hz, 16bit, stereo)");
    return *this;
}

// ============================================================================
// start — 创建 GMF 池 + 音频渲染器 + feeder 任务
// ============================================================================

AudioManager& AudioManager::start()
{
    if (started_) return *this;

    // ---- 1. 初始化 GMF 池并注册转换元素 ----
    // 注册 ch_cvt / bit_cvt / rate_cvt 等基本转换器，
    // render 在 stream_open 时按需自动创建转换管线
    ESP_LOGI(TAG, "初始化 GMF pool ...");
    esp_gmf_pool_handle_t pool = nullptr;
    ESP_ERROR_CHECK(esp_gmf_pool_init(&pool));

    {
        esp_gmf_element_handle_t el = nullptr;

        esp_ae_ch_cvt_cfg_t chCvtCfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
        ESP_ERROR_CHECK(esp_gmf_ch_cvt_init(&chCvtCfg, &el));
        ESP_ERROR_CHECK(esp_gmf_pool_register_element(pool, el, nullptr));

        esp_ae_bit_cvt_cfg_t bitCvtCfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
        ESP_ERROR_CHECK(esp_gmf_bit_cvt_init(&bitCvtCfg, &el));
        ESP_ERROR_CHECK(esp_gmf_pool_register_element(pool, el, nullptr));

        esp_ae_rate_cvt_cfg_t rateCvtCfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
        ESP_ERROR_CHECK(esp_gmf_rate_cvt_init(&rateCvtCfg, &el));
        ESP_ERROR_CHECK(esp_gmf_pool_register_element(pool, el, nullptr));
    }

    pool_ = pool;

    // ---- 2. 注册默认解码器 ----
    // 先注册底层硬件解码器（AAC、MP3、FLAC 等），再注册简单解码器封装
    ESP_LOGI(TAG, "注册默认音频解码器 ...");
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();

    // ---- 3. 创建音频渲染器 ----
    ESP_LOGI(TAG, "创建 Audio Render (max_stream=%d) ...", MAX_INSTANCES);

    esp_audio_render_cfg_t renderCfg = {
        .max_stream_num  = MAX_INSTANCES,
        .out_writer      = writerCallback,
        .out_ctx         = this,
        .out_sample_info = {
            .sample_rate     = OUT_SAMPLE_RATE,
            .bits_per_sample = OUT_BITS_PER_SAMPLE,
            .channel         = OUT_CHANNELS,
        },
        .pool            = pool,
        .process_period  = (uint16_t)processPeriodMs_,
        .process_buf_align = 0,
    };

    esp_audio_render_handle_t render = nullptr;
    esp_audio_render_err_t err = esp_audio_render_create(&renderCfg, &render);
    if (err != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "esp_audio_render_create 失败: %d", err);
        abort();
    }
    render_ = render;

    // ---- 4. 启动 feeder 任务 ----
    started_ = true;
    if (xTaskCreatePinnedToCore(
            feederTaskFunc, "audio_feeder", 4096, this,
            configMAX_PRIORITIES - 2, nullptr, 1) != pdPASS) {
        ESP_LOGE(TAG, "创建 feeder 任务失败");
        abort();
    }

    ESP_LOGI(TAG, "AudioManager 启动完成");
    return *this;
}

// ============================================================================
// stop — 停止所有播放 + 销毁渲染器
// ============================================================================

void AudioManager::stop()
{
    if (!started_) return;
    started_ = false;

    vTaskDelay(pdMS_TO_TICKS(50));  // 等待 feeder 退出

    if (render_) {
        esp_audio_render_destroy(render_);
        render_ = nullptr;
    }

    if (pool_) {
        esp_gmf_pool_deinit(pool_);
        pool_ = nullptr;
    }

    // 清理所有实例（预加载 + 流式）
    for (auto& inst : instances_) {
        if (inst.id >= 0) {
            inst.active = false;
            if (inst.streamHandle) {
                esp_audio_render_stream_close(inst.streamHandle);
                inst.streamHandle = nullptr;
            }
            // 释放流式资源
            if (inst.resId < 0) {
                if (inst.decoder) {
                    esp_audio_simple_dec_close(inst.decoder);
                    inst.decoder = nullptr;
                }
                free(inst.encData);
                inst.encData = nullptr;
                free(inst.streamOutBuf);
                inst.streamOutBuf = nullptr;
            }
            inst.id = -1;
        }
    }

    // 清理所有自动释放的资源
    for (auto& res : resources_) {
        if (res.id >= 0 && res.autoUnload) {
            free(res.pcmData);
            res.pcmData = nullptr;
            res.pcmSize = 0;
            res.id = -1;
        }
    }

    if (codecDev_) {
        esp_codec_dev_close(codecDev_);
        // 注意：不在此处 delete codecDev，由析构处理
    }

    if (txHandle_) {
        i2s_channel_disable(txHandle_);
        i2s_del_channel(txHandle_);
        txHandle_ = nullptr;
    }

    esp_audio_simple_dec_unregister_default();

    ESP_LOGI(TAG, "AudioManager 已停止");
}

// ============================================================================
// 播放接口
// ============================================================================

AudioHandle AudioManager::play(AudioResource& res)
{
    if (!started_ || !res || !render_) return {};

    Lock lock(mutex_);

    ResourceData* rd = findResource(res.id_);
    if (!rd || !rd->pcmData || rd->pcmSize == 0) return {};

    int instId = createInstance(res.id_);
    if (instId < 0) return {};

    InstanceData* inst = findInstance(instId);
    if (!inst) return {};

    // 打开渲染流
    esp_audio_render_sample_info_t inInfo = {
        .sample_rate     = rd->sampleRate,
        .bits_per_sample = rd->bitsPerSample,
        .channel         = rd->channels,
    };

    esp_audio_render_stream_handle_t stream = nullptr;
    esp_audio_render_stream_get(render_, ESP_AUDIO_RENDER_STREAM_ID(inst->id), &stream);
    if (!stream) {
        ESP_LOGE(TAG, "获取 stream %d 失败", inst->id);
        freeInstance(inst->id);
        return {};
    }

    esp_audio_render_err_t err = esp_audio_render_stream_open(stream, &inInfo);
    if (err != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "stream_open %d 失败: %d", inst->id, err);
        freeInstance(inst->id);
        return {};
    }

    inst->streamHandle = stream;
    inst->active = true;
    inst->readPos = 0;
    inst->volume = 1.0f;

    return AudioHandle(instId);
}

AudioHandle AudioManager::play(const char* uri)
{
    if (!started_ || !render_) return {};

    // 1. 读取编码文件（不解码，通常远小于 PCM）
    FILE* fp = fopen(uri, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "无法打开: %s", uri);
        return {};
    }
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fileSize <= 0) { fclose(fp); return {}; }

    uint8_t* encData = (uint8_t*)malloc(fileSize);
    if (!encData || fread(encData, 1, fileSize, fp) != (size_t)fileSize) {
        free(encData);
        fclose(fp);
        return {};
    }
    fclose(fp);

    // 2. 创建解码器，解码首帧获取采样信息
    esp_audio_simple_dec_type_t decType = guessDecType(uri);
    esp_audio_simple_dec_cfg_t decCfg = {
        .dec_type = decType, .dec_cfg = nullptr, .cfg_size = 0, .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t dec = nullptr;
    if (esp_audio_simple_dec_open(&decCfg, &dec) != ESP_AUDIO_ERR_OK) {
        free(encData);
        ESP_LOGE(TAG, "打开解码器失败: %s", uri);
        return {};
    }

    uint8_t* outBuf = (uint8_t*)malloc(64 * 1024);
    esp_audio_simple_dec_raw_t raw = {
        .buffer = encData, .len = (uint32_t)fileSize, .eos = true,
    };
    esp_audio_simple_dec_out_t outFrame = {
        .buffer = outBuf, .len = 64 * 1024,
    };

    // 解码首帧（可能需多次调用以获得解码信息）
    for (int i = 0; i < 5; i++) {
        (void)esp_audio_simple_dec_process(dec, &raw, &outFrame);
        if (outFrame.decoded_size > 0 || raw.consumed == 0) break;
        raw.buffer += raw.consumed;
        raw.len    -= raw.consumed;
        raw.consumed = 0;
    }

    esp_audio_simple_dec_info_t decInfo = {};
    esp_audio_simple_dec_get_info(dec, &decInfo);
    uint32_t sampleRate    = decInfo.sample_rate ? decInfo.sample_rate : OUT_SAMPLE_RATE;
    uint8_t  channels      = decInfo.channel      ? decInfo.channel      : 1;
    uint8_t  bitsPerSample = decInfo.bits_per_sample ? decInfo.bits_per_sample : 16;

    ESP_LOGI(TAG, "流式 %s: %uHz %uch %ubit", uri, sampleRate, channels, bitsPerSample);

    // 重置解码器，从头开始流式解码
    esp_audio_simple_dec_reset(dec);

    // 3. 分配实例 ID（不创建 ResourceData）
    Lock lock(mutex_);
    int instId = nextInstId_++;
    int idx = allocInstance();
    if (idx < 0) {
        free(encData); free(outBuf);
        esp_audio_simple_dec_close(dec);
        return {};
    }

    auto& inst = instances_[idx];
    inst.id                 = instId;
    inst.resId              = -1;  // 标记流式
    inst.active             = false;
    inst.volume             = 1.0f;
    inst.encData            = encData;
    inst.encSize            = (uint32_t)fileSize;
    inst.encPos             = 0;
    inst.decoder            = dec;
    inst.streamSampleRate   = sampleRate;
    inst.streamChannels     = channels;
    inst.streamBitsPerSample = bitsPerSample;
    inst.streamOutBuf       = outBuf;

    // 4. 打开渲染流
    esp_audio_render_sample_info_t inInfo = {
        .sample_rate = sampleRate, .bits_per_sample = bitsPerSample, .channel = channels,
    };
    esp_audio_render_stream_handle_t stream = nullptr;
    esp_audio_render_stream_get(render_, ESP_AUDIO_RENDER_STREAM_ID(instId), &stream);
    if (!stream || esp_audio_render_stream_open(stream, &inInfo) != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "stream_open 失败");
        free(encData); free(outBuf);
        esp_audio_simple_dec_close(dec);
        freeInstance(idx);
        return {};
    }

    inst.streamHandle = stream;
    inst.active = true;

    ESP_LOGI(TAG, "流式播放开始: %s", uri);
    return AudioHandle(instId);
}

// ============================================================================
// 音量控制
// ============================================================================

void AudioManager::setMasterVolume(float vol)
{
    if (!codecDev_) return;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    int codecVol = (int)(vol * 100.0f);
    if (codecVol < 0) codecVol = 0;
    if (codecVol > 100) codecVol = 100;
    esp_codec_dev_set_out_vol(codecDev_, codecVol);
}

void AudioManager::writePcmDirect(uint8_t* data, uint32_t len)
{
    if (codecDev_) {
        esp_codec_dev_write(codecDev_, data, len);
    }
}

void AudioManager::stopAll()
{
    Lock lock(mutex_);
    for (auto& inst : instances_) {
        if (inst.id >= 0 && inst.active) {
            destroyInstance(inst.id);
        }
    }
}

bool AudioManager::isPlaying() const
{
    Lock lock(mutex_);
    for (auto& inst : instances_) {
        if (inst.id >= 0 && inst.active) return true;
    }
    return false;
}

// ============================================================================
// 内部：资源管理
// ============================================================================

int AudioManager::preloadInternal(const char* uri, bool autoUnload)
{
    ESP_LOGI(TAG, "预加载: %s", uri);

    // ---- 1. 读取文件到内存 ----
    FILE* fp = fopen(uri, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "无法打开文件: %s", uri);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(fp);
        ESP_LOGE(TAG, "文件为空: %s", uri);
        return -1;
    }

    uint8_t* fileData = (uint8_t*)malloc(fileSize);
    if (!fileData) {
        fclose(fp);
        ESP_LOGE(TAG, "无法分配 %ld 字节读取文件", fileSize);
        return -1;
    }

    size_t bytesRead = fread(fileData, 1, fileSize, fp);
    fclose(fp);

    if (bytesRead != (size_t)fileSize) {
        free(fileData);
        ESP_LOGE(TAG, "读取文件不完整: %s", uri);
        return -1;
    }

    // ---- 2. 创建简单解码器 ----
    esp_audio_simple_dec_type_t decType = guessDecType(uri);

    esp_audio_simple_dec_cfg_t decCfg = {
        .dec_type      = decType,
        .dec_cfg       = nullptr,
        .cfg_size      = 0,
        .use_frame_dec = false,
    };
    esp_audio_simple_dec_handle_t dec = nullptr;
    esp_audio_err_t ret = esp_audio_simple_dec_open(&decCfg, &dec);
    if (ret != ESP_AUDIO_ERR_OK) {
        free(fileData);
        ESP_LOGE(TAG, "打开解码器失败: %d", ret);
        return -1;
    }

    // ---- 3. 逐帧解码到 PSRAM ----
    uint8_t* pcmBuf = nullptr;
    uint32_t pcmTotal = 0;
    uint32_t pcmCapacity = 0;
    bool infoGot = false;  // 是否已获取采样信息

    constexpr uint32_t OUT_BUF_SIZE = 64 * 1024;
    uint8_t* outBuf = (uint8_t*)malloc(OUT_BUF_SIZE);
    if (!outBuf) {
        esp_audio_simple_dec_close(dec);
        free(fileData);
        ESP_LOGE(TAG, "无法分配解码输出缓冲");
        return -1;
    }

    esp_audio_simple_dec_raw_t raw = {
        .buffer = fileData,
        .len    = (uint32_t)fileSize,
        .eos    = true,
    };

    while (raw.len > 0) {
        esp_audio_simple_dec_out_t outFrame = {
            .buffer = outBuf,
            .len    = OUT_BUF_SIZE,
        };

        ret = esp_audio_simple_dec_process(dec, &raw, &outFrame);
        if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_CONTINUE) {
            ESP_LOGW(TAG, "解码过程返回: %d", ret);
            break;
        }

        // 首次成功解码后获取采样信息
        if (!infoGot && outFrame.decoded_size > 0) {
            esp_audio_simple_dec_info_t decInfo;
            if (esp_audio_simple_dec_get_info(dec, &decInfo) == ESP_AUDIO_ERR_OK) {
                ESP_LOGI(TAG, "解码器信息: %uHz %uch %ubit",
                         decInfo.sample_rate, decInfo.channel, decInfo.bits_per_sample);
            }
            infoGot = true;
        }

        if (outFrame.decoded_size > 0) {
            uint32_t needed = pcmTotal + outFrame.decoded_size;
            if (needed > pcmCapacity) {
                uint32_t newCap = pcmCapacity ? pcmCapacity * 2 : 128 * 1024;
                while (newCap < needed) newCap *= 2;
                uint8_t* newBuf = (uint8_t*)realloc(pcmBuf, newCap);
                if (!newBuf) {
                    ESP_LOGE(TAG, "PCM 缓冲扩展失败 (需要 %u)", needed);
                    free(outBuf);
                    free(pcmBuf);
                    free(fileData);
                    esp_audio_simple_dec_close(dec);
                    return -1;
                }
                pcmBuf = newBuf;
                pcmCapacity = newCap;
            }
            memcpy(pcmBuf + pcmTotal, outBuf, outFrame.decoded_size);
            pcmTotal += outFrame.decoded_size;
        }

        if (raw.consumed > 0) {
            raw.buffer += raw.consumed;
            raw.len    -= raw.consumed;
            raw.consumed = 0;
        } else {
            break;
        }
    }

    // ---- 4. 获取最终采样信息 ----
    uint32_t sampleRate   = OUT_SAMPLE_RATE;
    uint8_t  channels     = OUT_CHANNELS;
    uint8_t  bitsPerSample = OUT_BITS_PER_SAMPLE;

    if (infoGot) {
        esp_audio_simple_dec_info_t decInfo;
        if (esp_audio_simple_dec_get_info(dec, &decInfo) == ESP_AUDIO_ERR_OK) {
            sampleRate    = decInfo.sample_rate;
            channels      = decInfo.channel;
            bitsPerSample = decInfo.bits_per_sample;
        }
    } else {
        ESP_LOGW(TAG, "未能获取解码信息，使用默认输出参数");
    }

    free(outBuf);
    free(fileData);
    esp_audio_simple_dec_close(dec);

    if (pcmTotal == 0) {
        free(pcmBuf);
        ESP_LOGE(TAG, "解码结果为空: %s", uri);
        return -1;
    }

    // ---- 5. 存入资源表 ----
    Lock lock(mutex_);

    int idx = allocResource();
    if (idx < 0) {
        free(pcmBuf);
        ESP_LOGE(TAG, "资源表已满");
        return -1;
    }

    resources_[idx].id            = nextResId_++;
    resources_[idx].pcmData       = pcmBuf;
    resources_[idx].pcmSize       = pcmTotal;
    resources_[idx].sampleRate    = sampleRate;
    resources_[idx].channels      = channels;
    resources_[idx].bitsPerSample = bitsPerSample;
    resources_[idx].refCount      = 0;
    resources_[idx].autoUnload    = autoUnload;

    ESP_LOGI(TAG, "预加载完成: id=%d, size=%u, %uHz %uch %ubit",
             resources_[idx].id, pcmTotal, sampleRate, channels, bitsPerSample);

    return resources_[idx].id;
}

void AudioManager::unloadInternal(int resId)
{
    Lock lock(mutex_);
    for (auto& res : resources_) {
        if (res.id == resId) {
            if (res.refCount > 0) {
                ESP_LOGW(TAG, "资源 %d 仍有 %d 个引用，强制卸载", resId, res.refCount);
            }
            if (res.pcmData) {
                free(res.pcmData);
                res.pcmData = nullptr;
            }
            res.pcmSize = 0;
            res.id = -1;
            ESP_LOGI(TAG, "资源 %d 已卸载", resId);
            return;
        }
    }
}

// ============================================================================
// 内部：实例管理
// ============================================================================

int AudioManager::createInstance(int resId)
{
    int idx = allocInstance();
    if (idx < 0) return -1;

    instances_[idx].id     = nextInstId_++;
    instances_[idx].resId  = resId;
    instances_[idx].active = false;
    instances_[idx].readPos = 0;
    instances_[idx].volume = 1.0f;

    ResourceData* rd = findResource(resId);
    if (rd) rd->refCount++;

    return instances_[idx].id;
}

void AudioManager::destroyInstance(int instId)
{
    InstanceData* inst = findInstance(instId);
    if (!inst) return;

    inst->active = false;

    if (inst->streamHandle) {
        esp_audio_render_stream_close(inst->streamHandle);
        inst->streamHandle = nullptr;
    }

    if (inst->resId >= 0) {
        // 预加载模式
        ResourceData* rd = findResource(inst->resId);
        if (rd) {
            rd->refCount--;
            if (rd->refCount <= 0 && rd->autoUnload) {
                freeInstance(instId);
                unloadInternal(rd->id);
                return;
            }
        }
        freeInstance(instId);
    } else {
        // 流式模式
        destroyStreamInstance(instId);
    }
}

void AudioManager::destroyStreamInstance(int instId)
{
    InstanceData* inst = findInstance(instId);
    if (!inst) return;

    if (inst->decoder) {
        esp_audio_simple_dec_close(inst->decoder);
        inst->decoder = nullptr;
    }
    free(inst->encData);
    inst->encData = nullptr;
    free(inst->streamOutBuf);
    inst->streamOutBuf = nullptr;

    freeInstance(instId);
}

// ============================================================================
// writer 回调 — 由 render 混音线程调用
// ============================================================================

int AudioManager::writerCallback(uint8_t* pcm, uint32_t len, void* ctx)
{
    AudioManager* self = static_cast<AudioManager*>(ctx);
    if (!self || !self->codecDev_) return -1;

    static int wrCbCount = 0;
    if (++wrCbCount <= 3) {
        ESP_LOGI(TAG, "writerCallback 被调用 #%d, len=%u", wrCbCount, len);
    }

    int ret = esp_codec_dev_write(self->codecDev_, pcm, len);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec_dev_write 失败: %d", ret);
    }
    return (ret == ESP_CODEC_DEV_OK) ? 0 : -1;
}

// ============================================================================
// feeder 任务 — 持续向流中喂 PCM 数据
// ============================================================================

void AudioManager::feederTaskFunc(void* arg)
{
    AudioManager* self = static_cast<AudioManager*>(arg);
    while (self->started_) {
        self->feedActiveStreams();
        vTaskDelay(pdMS_TO_TICKS(self->processPeriodMs_));
    }
    vTaskDelete(nullptr);
}

void AudioManager::feedActiveStreams()
{
    int pendingUnload[MAX_INSTANCES];
    int pendingCount = 0;

    {
        Lock lock(mutex_);

        for (auto& inst : instances_) {
            if (!inst.active || !inst.streamHandle) continue;

            if (inst.resId >= 0) {
                // ============================================================
                // 预加载模式 — 从 PCM 缓存读取
                // ============================================================
                ResourceData* rd = findResource(inst.resId);
                if (!rd || !rd->pcmData || rd->pcmSize == 0) {
                    esp_audio_render_stream_close(inst.streamHandle);
                    inst.streamHandle = nullptr;
                    inst.active = false;
                    continue;
                }

                uint32_t bytesPerSec = rd->sampleRate * rd->channels * (rd->bitsPerSample / 8);
                uint32_t chunkSize = (bytesPerSec * processPeriodMs_) / 1000;
                chunkSize = (chunkSize + 3) & ~3;
                if (chunkSize == 0) chunkSize = 512;

                if (inst.readPos >= rd->pcmSize) {
                    if (inst.loop) {
                        inst.readPos = 0;
                    } else {
                        esp_audio_render_stream_close(inst.streamHandle);
                        inst.streamHandle = nullptr;
                        inst.active = false;
                        if (rd->autoUnload) {
                            pendingUnload[pendingCount++] = rd->id;
                        }
                        freeInstance(inst.id);
                    }
                    continue;
                }

                uint32_t remaining = rd->pcmSize - inst.readPos;
                uint32_t toWrite = (chunkSize < remaining) ? chunkSize : remaining;

                if (inst.volume < 1.0f && inst.volume >= 0.0f) {
                    uint8_t* scaledBuf = (uint8_t*)malloc(toWrite);
                    if (scaledBuf) {
                        memcpy(scaledBuf, rd->pcmData + inst.readPos, toWrite);
                        if (rd->bitsPerSample == 16) {
                            int16_t* samples = (int16_t*)scaledBuf;
                            int sampleCount = toWrite / 2;
                            for (int i = 0; i < sampleCount; i++) {
                                samples[i] = (int16_t)(samples[i] * inst.volume);
                            }
                        }
                        esp_audio_render_stream_write(inst.streamHandle, scaledBuf, toWrite);
                        free(scaledBuf);
                    } else {
                        esp_audio_render_stream_write(inst.streamHandle,
                            rd->pcmData + inst.readPos, toWrite);
                    }
                } else {
                    esp_audio_render_stream_write(inst.streamHandle,
                        rd->pcmData + inst.readPos, toWrite);
                }
                inst.readPos += toWrite;

            } else {
                // ============================================================
                // 流式模式 — 实时解码并喂数据
                // ============================================================
                if (!inst.decoder || !inst.encData) {
                    esp_audio_render_stream_close(inst.streamHandle);
                    inst.streamHandle = nullptr;
                    inst.active = false;
                    continue;
                }

                if (inst.encPos < inst.encSize) {
                    // 多次尝试解码，直到拿到一帧或数据耗尽
                    for (int attempt = 0; attempt < 3; attempt++) {
                        uint32_t remaining = inst.encSize - inst.encPos;
                        esp_audio_simple_dec_raw_t raw = {
                            .buffer = inst.encData + inst.encPos,
                            .len    = remaining,
                            .eos    = false,  // 非末帧，不设 eos
                        };
                        esp_audio_simple_dec_out_t out = {
                            .buffer = inst.streamOutBuf,
                            .len    = inst.streamOutBufSize,
                        };

                        (void)esp_audio_simple_dec_process(
                            inst.decoder, &raw, &out);

                        inst.encPos += raw.consumed;

                        if (out.decoded_size > 0) {
                            static int feedCount = 0;
                            if (++feedCount % 50 == 0) {
                                ESP_LOGI(TAG, "流式解码中: encPos=%u/%u, decoded=%u",
                                         inst.encPos, inst.encSize, out.decoded_size);
                            }
                            if (inst.volume < 1.0f && inst.volume >= 0.0f
                                && inst.streamBitsPerSample == 16) {
                                int16_t* samples = (int16_t*)inst.streamOutBuf;
                                int cnt = out.decoded_size / 2;
                                for (int i = 0; i < cnt; i++) {
                                    samples[i] = (int16_t)(samples[i] * inst.volume);
                                }
                            }
                            esp_audio_render_err_t wrErr = esp_audio_render_stream_write(
                                inst.streamHandle, inst.streamOutBuf, out.decoded_size);
                            if (wrErr != ESP_AUDIO_RENDER_ERR_OK) {
                                static int wrErrCount = 0;
                                if (++wrErrCount <= 3) {
                                    ESP_LOGE(TAG, "stream_write 失败: %d", wrErr);
                                }
                            }
                        }

                        if (raw.consumed == 0) break;   // 解码器需要更多数据
                        if (out.decoded_size > 0) break; // 已获得一帧
                    }
                }

                // 检查是否播放完毕（最后再用 eos=true flush 一次）
                if (inst.encPos >= inst.encSize) {
                    uint8_t dummyBuf[16];
                    esp_audio_simple_dec_raw_t raw = {
                        .buffer = dummyBuf, .len = 0, .eos = true,
                    };
                    esp_audio_simple_dec_out_t out = {
                        .buffer = inst.streamOutBuf, .len = inst.streamOutBufSize,
                    };
                    (void)esp_audio_simple_dec_process(
                        inst.decoder, &raw, &out);
                    if (out.decoded_size > 0) {
                        esp_audio_render_stream_write(inst.streamHandle,
                            inst.streamOutBuf, out.decoded_size);
                    }

                    if (inst.loop) {
                        esp_audio_simple_dec_reset(inst.decoder);
                        inst.encPos = 0;
                        ESP_LOGD(TAG, "流式循环: instance %d", inst.id);
                    } else {
                        esp_audio_render_stream_close(inst.streamHandle);
                        inst.streamHandle = nullptr;
                        inst.active = false;
                        ESP_LOGI(TAG, "流式播放结束: instance %d", inst.id);
                        destroyStreamInstance(inst.id);
                    }
                }
            }
        }
    }

    for (int i = 0; i < pendingCount; i++) {
        unloadInternal(pendingUnload[i]);
    }
}

// ============================================================================
// AudioResource
// ============================================================================

AudioResource::AudioResource(const char* uri)
{
    id_ = AudioManager::instance().preloadInternal(uri, false);
}

AudioResource::~AudioResource()
{
    if (id_ >= 0) {
        AudioManager::instance().unloadInternal(id_);
    }
}

// ============================================================================
// AudioHandle
// ============================================================================

AudioHandle::~AudioHandle()
{
    if (id_ >= 0 && loop_) {
        AudioManager::instance().destroyInstance(id_);
    }
}

AudioHandle& AudioHandle::setVolume(float vol)
{
    auto& inst = AudioManager::instance();
    Lock lock(inst.mutex_);
    auto* data = inst.findInstance(id_);
    if (data) data->volume = vol;
    return *this;
}

AudioHandle& AudioHandle::setLoop(bool loop)
{
    loop_ = loop;
    auto& inst = AudioManager::instance();
    Lock lock(inst.mutex_);
    auto* data = inst.findInstance(id_);
    if (data) data->loop = loop;
    return *this;
}

void AudioHandle::stop()
{
    AudioManager::instance().destroyInstance(id_);
    id_ = -1;
}

bool AudioHandle::isPlaying() const
{
    auto& inst = AudioManager::instance();
    Lock lock(inst.mutex_);
    auto* data = inst.findInstance(id_);
    return data && data->active;
}

// ============================================================================
// 内部辅助：资源/实例表操作
// ============================================================================

AudioManager::ResourceData* AudioManager::findResource(int resId)
{
    for (auto& res : resources_) {
        if (res.id == resId) return &res;
    }
    return nullptr;
}

AudioManager::InstanceData* AudioManager::findInstance(int instId)
{
    for (auto& inst : instances_) {
        if (inst.id == instId) return &inst;
    }
    return nullptr;
}

int AudioManager::allocResource()
{
    for (int i = 0; i < MAX_RESOURCES; i++) {
        if (resources_[i].id < 0) return i;
    }
    return -1;
}

int AudioManager::allocInstance()
{
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (instances_[i].id < 0) return i;
    }
    return -1;
}

void AudioManager::freeResource(int idx)
{
    if (idx >= 0 && idx < MAX_RESOURCES) {
        resources_[idx].id = -1;
    }
}

void AudioManager::freeInstance(int idx)
{
    if (idx >= 0 && idx < MAX_INSTANCES) {
        instances_[idx].id = -1;
        instances_[idx].streamHandle = nullptr;
        instances_[idx].active = false;
    }
}

AudioManager::~AudioManager()
{
    // 析构但不要销毁单例
}

// ============================================================================
// 工具函数
// ============================================================================

const char* audioExtToTypeStr(const char* uri)
{
    const char* ext = strrchr(uri, '.');
    if (!ext) return "unknown";

    char lower[8] = {};
    for (int i = 0; ext[i] && i < 7; i++) {
        char c = ext[i];
        if (c >= 'A' && c <= 'Z') c += 0x20;
        lower[i] = c;
    }

    if      (strcmp(lower, ".wav")  == 0) return "WAV";
    else if (strcmp(lower, ".mp3")  == 0) return "MP3";
    else if (strcmp(lower, ".aac")  == 0) return "AAC";
    else if (strcmp(lower, ".m4a")  == 0) return "M4A";
    else if (strcmp(lower, ".flac") == 0) return "FLAC";
    else if (strcmp(lower, ".ogg")  == 0) return "OGG";
    else                                 return "PCM";
}
