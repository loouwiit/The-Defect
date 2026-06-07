#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include "esp_err.h"
#include "iic/iic.hpp"
#include "mutex/mutex.hpp"

// 前向声明（具体类型在 .cpp 中 Include）
#include "driver/i2s_common.h"
#include "esp_codec_dev.h"
#include "esp_audio_render.h"
#include "esp_gmf_pool.h"

class AudioManager;

// ============================================================================
// AudioResource — RAII 预加载资源
// ============================================================================
// 构造时预加载指定 URI 的音频文件，解码为 PCM 缓存于 PSRAM。
// 析构时自动释放 PCM 缓存。
// 支持移动语义，不可拷贝。
class AudioResource {
public:
    AudioResource() = default;
    explicit AudioResource(const char* uri);
    ~AudioResource();

    AudioResource(const AudioResource&) = delete;
    AudioResource& operator=(const AudioResource&) = delete;

    AudioResource(AudioResource&& other) noexcept
        : id_(other.id_) { other.id_ = -1; }

    AudioResource& operator=(AudioResource&& other) noexcept {
        if (this != &other) {
            id_ = other.id_;
            other.id_ = -1;
        }
        return *this;
    }

    explicit operator bool() const { return id_ >= 0; }
    int id() const { return id_; }

private:
    int id_ = -1;
    friend class AudioManager;
};

// ============================================================================
// AudioHandle — 播放句柄
// ============================================================================
// 控制单个播放实例的音量、循环、停止等。
// 析构行为：
//   - 若 loop==true: 自动调用 stop() 停止播放
//   - 若 loop==false: 不做任何事（播放结束后自动清理）
// 支持移动语义，不可拷贝。
class AudioHandle {
public:
    AudioHandle() = default;
    ~AudioHandle();

    AudioHandle(const AudioHandle&) = delete;
    AudioHandle& operator=(const AudioHandle&) = delete;

    AudioHandle(AudioHandle&& other) noexcept
        : id_(other.id_), loop_(other.loop_) { other.id_ = -1; }

    AudioHandle& operator=(AudioHandle&& other) noexcept {
        if (this != &other) {
            id_ = other.id_;
            loop_ = other.loop_;
            other.id_ = -1;
        }
        return *this;
    }

    explicit operator bool() const { return id_ >= 0; }

    /// 设置音量 0.0 ~ 1.0（链式调用）
    AudioHandle& setVolume(float vol);
    /// 设置是否循环（链式调用）
    AudioHandle& setLoop(bool loop);
    /// 主动停止播放
    void stop();
    /// 是否正在播放
    bool isPlaying() const;

private:
    int id_ = -1;
    bool loop_ = false;

    AudioHandle(int id) : id_(id) {}
    friend class AudioManager;
};

// ============================================================================
// AudioManager — 音频管理器（单例）
// ============================================================================
// 职责：
//   - 初始化 I2S + ES8311 音频编解码器
//   - 管理 esp_audio_render 混音渲染器
//   - 提供预加载（Resource）和即播（play uri）两种播放方式
//   - 后台 feeder 任务持续向混音器输送 PCM 数据
class AudioManager {
public:
    static AudioManager& instance();

    /// 初始化音频硬件（I2S + 编解码器）
    AudioManager& init(IIC& iic);
    /// 启动渲染器和 feeder 任务
    AudioManager& start();
    /// 停止所有播放并销毁渲染器
    void stop();

    // ---- 播放接口 ----

    /// 从预加载的 Resource 播放（可多个实例同时播放同一 Resource）
    AudioHandle play(AudioResource& res);

    /// 直接播放 URI（内部预加载 → 播放 → 自动释放）
    AudioHandle play(const char* uri);

    // ---- 控制 ----

    /// 设置主音量 0.0 ~ 1.0（通过 ES8311 硬件寄存器）
    void setMasterVolume(float vol);
    /// 调试：直接写 PCM 到 codec（绕过 render，用于硬件验证）
    void writePcmDirect(uint8_t* data, uint32_t len);
    /// 停止所有播放实例
    void stopAll();
    /// 是否有任何实例正在播放
    bool isPlaying() const;

private:
    AudioManager() = default;
    ~AudioManager();
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    friend class AudioResource;
    friend class AudioHandle;

    // ---- 内部数据结构 ----

    static constexpr int MAX_RESOURCES = 16;
    static constexpr int MAX_INSTANCES = 1;  // 临时开 1 来 bypass mixer 线程

    struct ResourceData {
        int     id = -1;
        uint8_t* pcmData = nullptr;
        uint32_t pcmSize = 0;
        uint32_t sampleRate = 0;
        uint8_t  channels = 0;
        uint8_t  bitsPerSample = 0;
        int      refCount = 0;       // 引用计数
        bool     autoUnload = false; // play(uri) 一次性使用，播完自动释放
    };

    struct InstanceData {
        int     id = -1;
        int     resId = -1;          // >=0:预加载; <0:流式
        void*   streamHandle = nullptr;
        bool    loop = false;
        bool    active = false;
        float   volume = 1.0f;

        // 预加载模式 (resId >= 0)
        size_t  readPos = 0;

        // 流式模式 (resId < 0) — 编码数据 + 解码器
        uint8_t* encData      = nullptr;   // 编码文件数据
        uint32_t encSize      = 0;         // 编码文件大小
        uint32_t encPos       = 0;         // 当前解码位置
        void*    decoder      = nullptr;   // esp_audio_simple_dec_handle_t
        uint32_t streamSampleRate    = 0;
        uint8_t  streamChannels      = 0;
        uint8_t  streamBitsPerSample = 0;
        uint8_t* streamOutBuf        = nullptr;
        uint32_t streamOutBufSize    = 64 * 1024;
    };

    // ---- 内部方法 ----

    int  preloadInternal(const char* uri, bool autoUnload);
    void unloadInternal(int resId);
    int  createInstance(int resId);
    void destroyInstance(int instId);
    void destroyStreamInstance(int instId);

    static int writerCallback(uint8_t* pcm, uint32_t len, void* ctx);
    static void feederTaskFunc(void* arg);
    void feedActiveStreams();

    ResourceData* findResource(int resId);
    InstanceData* findInstance(int instId);
    int allocResource();
    int allocInstance();
    void freeResource(int idx);
    void freeInstance(int idx);

    // ---- 成员 ----

    esp_codec_dev_handle_t  codecDev_    = nullptr;
    esp_audio_render_handle_t render_      = nullptr;
    esp_gmf_pool_handle_t     pool_        = nullptr;
    i2s_chan_handle_t         txHandle_    = nullptr;
    bool  started_     = false;
    int   processPeriodMs_ = 20;

    ResourceData resources_[MAX_RESOURCES];
    InstanceData instances_[MAX_INSTANCES];
    Mutex mutex_;
    int   nextResId_  = 0;
    int   nextInstId_ = 0;
};

// ---- 工具函数 ----

/// 根据文件扩展名猜测音频类型
const char* audioExtToTypeStr(const char* uri);
