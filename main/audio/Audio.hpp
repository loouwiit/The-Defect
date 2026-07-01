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

#include "storage/fat.hpp"

class ES8311;
class Audio;  // forward declaration for AudioHandle::Impl

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

        uint8_t* encBuf{};
        uint8_t* pcmBuf{};
        size_t   pcmBufSize{};

        esp_audio_render_sample_info_t inputInfo{};
        esp_audio_render_stream_id_t   streamId{};

        std::atomic<bool> running{false};
        std::atomic<bool> stopRequested{false};

        std::atomic<float> volume{1.0f};
        bool  loop{false};
        bool  detached{false};
        bool  started{false};

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

    /** 创建一个音频播放句柄（不开流，不启动解码任务） */
    static AudioHandle play(const char* path);

private:
    Audio()  = default;
    ~Audio() { deinit(); }
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    static int outWriter(uint8_t* pcm, uint32_t size, void* ctx);

    static esp_audio_type_t detectFormat(const char* path);

    esp_audio_render_handle_t render{};
    esp_gmf_pool_handle_t     pool{};
    ES8311*                   codec{};
    bool                      initialized{};

    uint8_t streamUsedMask{};   // 流分配位图，bit n = stream n 已分配

    /** 分配一个空闲的 stream ID，返回 0xFF 表示无空闲 */
    esp_audio_render_stream_id_t allocStreamId();
    /** 释放 stream ID */
    void freeStreamId(esp_audio_render_stream_id_t id);

    static constexpr uint8_t MaxStreamNum = 8;
    static constexpr size_t  TaskStackSize = 4096;

    friend class AudioHandle;
    friend struct AudioHandle::Impl;
};
