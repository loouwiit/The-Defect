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
    if (self->stream) {
        esp_audio_render_stream_close(self->stream);
        if (self->manager) {
            self->manager->freeStreamId(self->streamId);
        }
        self->stream = nullptr;
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
    // ── 1. 解码第一帧，获取音频信息 ──
    size_t firstRead = file.read(encBuf, EncBufSize);
    if (firstRead == 0) {
        ESP_LOGE(TAG, "无法读取音频文件");
        return;
    }

    esp_audio_dec_in_raw_t raw = { .buffer = encBuf, .len = firstRead };
    esp_audio_dec_out_frame_t frame = { .buffer = pcmBuf, .len = pcmBufSize };

    auto ret = esp_audio_dec_process(dec, &raw, &frame);

    // 处理缓冲区大小不足：按 needed_size 重新分配并重试
    if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && frame.needed_size > pcmBufSize) {
        ESP_LOGI(TAG, "PCM 缓冲区从 %u 扩展到 %lu", pcmBufSize, frame.needed_size);
        delete[] pcmBuf;
        pcmBufSize = frame.needed_size;
        pcmBuf = new uint8_t[pcmBufSize];
        if (pcmBuf) {
            frame.buffer = pcmBuf;
            frame.len    = pcmBufSize;
            // raw 未被消耗，直接用相同输入重试
            raw.buffer = encBuf;
            raw.len    = firstRead;
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
        inputInfo.sample_rate    = info.sample_rate;
        inputInfo.bits_per_sample = info.bits_per_sample;
        inputInfo.channel        = info.channel;
        ESP_LOGI(TAG, "音频参数: %lu Hz, %u bit, %u ch",
                 inputInfo.sample_rate, inputInfo.bits_per_sample, inputInfo.channel);
    } else {
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
    raw.len    -= raw.consumed;

    // ── 5. 主解码循环 ──
    while (!stopRequested.load()) {
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
                raw = { .buffer = encBuf, .len = 0 };
                continue;
            }
            break;
        }

        // 5d. 解码
        frame.buffer = pcmBuf;
        frame.len    = pcmBufSize;
        ret = esp_audio_dec_process(dec, &raw, &frame);

        if (ret == ESP_AUDIO_ERR_OK) {
            raw.buffer += raw.consumed;
            raw.len    -= raw.consumed;

            // 应用运行时音量
            applyVolume(reinterpret_cast<int16_t*>(pcmBuf),
                        frame.decoded_size / sizeof(int16_t),
                        volume.load());

            // 写入 render（多流模式 → ringbuffer）
            esp_audio_render_stream_write(stream, pcmBuf, frame.decoded_size);
        } else if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            if (frame.needed_size > pcmBufSize) {
                ESP_LOGW(TAG, "PCM 缓冲区从 %u 扩展到 %lu", pcmBufSize, frame.needed_size);
                auto* newBuf = new uint8_t[frame.needed_size];
                if (newBuf) {
                    delete[] pcmBuf;
                    pcmBuf    = newBuf;
                    pcmBufSize = frame.needed_size;
                }
            }
            // raw 未被消耗，重试
            frame.buffer = pcmBuf;
            frame.len    = pcmBufSize;
            ret = esp_audio_dec_process(dec, &raw, &frame);
            if (ret == ESP_AUDIO_ERR_OK) {
                raw.buffer += raw.consumed;
                raw.len    -= raw.consumed;
                applyVolume(reinterpret_cast<int16_t*>(pcmBuf),
                            frame.decoded_size / sizeof(int16_t),
                            volume.load());
                esp_audio_render_stream_write(stream, pcmBuf, frame.decoded_size);
            } else {
                ESP_LOGW(TAG, "重试后仍解码错误: %d, 跳过", ret);
                raw.buffer += raw.consumed;
                raw.len    -= raw.consumed;
            }
        } else {
            ESP_LOGW(TAG, "解码错误: %d, 跳过", ret);
            raw.buffer += raw.consumed;
            raw.len    -= raw.consumed;
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
        return;
    }

    // 分配编码/解码缓冲区
    impl->encBuf = new (std::nothrow) uint8_t[Impl::EncBufSize];
    impl->pcmBuf = new (std::nothrow) uint8_t[Impl::PcmBufSize];
    if (!impl->encBuf || !impl->pcmBuf) {
        ESP_LOGE(TAG, "分配音频缓冲区失败");
        delete[] impl->encBuf;
        delete[] impl->pcmBuf;
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
//  Audio
// ══════════════════════════════════════════════

Audio& Audio::instance()
{
    static Audio inst;
    return inst;
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
        ch_cvt_cfg.sample_rate     = 48000;
        ch_cvt_cfg.bits_per_sample = 16;
        ch_cvt_cfg.src_ch          = 1;   // 输入可能是单声道
        ch_cvt_cfg.dest_ch         = 2;   // 统一转立体声
        if (esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
            esp_gmf_pool_register_element(pool, el, nullptr);
        }

        esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
        bit_cvt_cfg.sample_rate = 48000;
        bit_cvt_cfg.channel     = 2;
        bit_cvt_cfg.src_bits    = 16;
        bit_cvt_cfg.dest_bits   = 16;
        if (esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
            esp_gmf_pool_register_element(pool, el, nullptr);
        }

        esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
        rate_cvt_cfg.src_rate        = 44100;
        rate_cvt_cfg.dest_rate       = 48000;
        rate_cvt_cfg.channel         = 2;
        rate_cvt_cfg.bits_per_sample = 16;
        if (esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el) == ESP_GMF_ERR_OK) {
            esp_gmf_pool_register_element(pool, el, nullptr);
        }
    }

    // 创建 audio render
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = MaxStreamNum,
        .out_writer     = outWriter,
        .out_ctx        = this,
        .out_sample_info = {
            .sample_rate    = 48000,
            .bits_per_sample = 16,
            .channel        = 2,
        },
        .pool           = pool,
        .process_period = 20,
    };

    if (esp_audio_render_create(&cfg, &render) != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "audio render 创建失败");
        esp_gmf_pool_deinit(pool);
        pool = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "audio render 已创建 (max_stream=%u)", MaxStreamNum);

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
    esp_audio_dec_cfg_t decCfg = { .type = type };
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

// ── 流分配 ──

esp_audio_render_stream_id_t Audio::allocStreamId()
{
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
    if (id < MaxStreamNum) {
        streamUsedMask &= ~(1 << id);
    }
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
