#pragma once
/*
 * ElfManager — ELF 插件模块管理（main 单例）
 *
 * 职责：
 *   - 模块缓存：path → { dlopen handle, elf_plugin_info_t*, refcount, size, mtime }
 *   - 同步加载：dlopen 必须在内部 RAM 静态栈任务上执行（esp_elf_arch_flush 约束），
 *     故使用持久 internal-RAM worker task + 信号量；dlopen/dlsym/版本校验/热更新检测
 *   - createApp：refcount++ → info->create_app → 记录 App↔模块绑定（App::m_elfModule）
 *   - onAppDestroyed：AppStack 在 delete App 后调用 → refcount-- → 0 时 dlclose
 *     （dlclose 无 arch_flush，普通 PSRAM 栈可执行）
 *   - 热更新：ensureLoaded 时比对文件 size/mtime，变化则重载（refcount==0 前提）
 */
#include <cstdint>
#include <cstddef>
#include <time.h>
#include "mutex/mutex.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class App;
class Display;
struct cJSON;
struct elf_plugin_info;

class ElfManager
{
public:
    static ElfManager& instance();

    /** 同步加载模块；失败（文件缺失/版本不符/符号缺失）返回 false */
    bool ensureLoaded(const char* fullPath);

    /** 从模块创建一个 App（App* 由 .so 工厂返回）；失败返回 nullptr */
    App* createApp(const char* fullPath, Display* display, const cJSON* params);

    /** AppStack 在 delete App 之后调用（参数为 App 上记录的 m_elfModule）；refcount-- → 0 时 dlclose */
    void onAppDestroyed(void* module);

    /** 强制卸载；refcount>0 时拒绝并返回 false */
    bool unload(const char* fullPath);

    // ── 状态查询（TestApp 面板） ──
    bool isLoaded() const;
    const char* path() const;      // 完整路径（如 /root/elf/smoke.so）
    const char* name() const;      // 插件名（info->name）
    uint32_t version() const;
    uint32_t refcount() const;
    uint32_t loadTimeMs() const;
    size_t moduleSize() const;     // 文件大小

private:
    ElfManager() = default;
    ElfManager(const ElfManager&) = delete;
    ElfManager& operator=(const ElfManager&) = delete;

    struct Module
    {
        void* handle = nullptr;
        const struct elf_plugin_info* info = nullptr;
        int refcount = 0;
        size_t size = 0;
        time_t mtime = 0;
        uint32_t loadTimeMs = 0;
        char path[64] = {};
    };

    struct LoadReq
    {
        const char* path = nullptr;
        bool ok = false;
        uint32_t loadTimeMs = 0;
        void* handle = nullptr;
        const struct elf_plugin_info* info = nullptr;
    };

    // ── 内部 RAM 加载 worker（持久任务） ──

    static void loadWorker(void* param);          // 持久 worker 主循环
    static void loadOne(LoadReq* req);      // 实际 dlopen + dlsym + 版本校验

    static StackType_t s_loadStack[4096];         // 内部 RAM 静态栈（勿加 EXT_RAM_BSS_ATTR）
    static StaticTask_t s_loadTcb;

    // ── 工具 ──
    static void relativePath(const char* fullPath, char* out, size_t n); // "/root/elf/x.so" → "elf/x.so"
    static bool statFile(const char* fullPath, size_t* size, time_t* mtime); // 成功返回 false

    Module m_module;
    mutable Mutex m_mutex;
    bool m_loaded = false;

    // 单次加载请求（单飞；R1 由 TestApp 串行驱动）
    LoadReq m_req;
    SemaphoreHandle_t m_reqSem = nullptr;
    volatile bool m_reqDone = false;
};
