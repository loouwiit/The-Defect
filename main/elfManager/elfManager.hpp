#pragma once
/*
 * ElfManager — ELF 插件模块管理（main 单例，多模块）
 *
 * 职责：
 *   - 模块缓存：path → { dlopen handle, elf_plugin_info_t*, refcount, size, mtime }
 *   - 同步加载：dlopen 必须在内部 RAM 静态栈任务上执行（esp_elf_arch_flush 约束），
 *     故使用持久 internal-RAM worker task + 信号量；dlopen/dlsym/版本校验/热更新检测
 *   - createApp：refcount++ → info->create_app → 记录 App↔模块绑定（App::m_elfModulePath）
 *   - onAppDestroyed：AppStack 在 delete App 后调用 → refcount-- → 0 时 dlclose
 *     （dlclose 无 arch_flush，普通 PSRAM 栈可执行）
 *   - 热更新：ensureLoaded 时比对文件 size/mtime，变化则重载（refcount==0 前提）
 *   - 元数据扫描：listElfFiles（快，只读目录）+ inspectModule（慢，dlopen→读→dlclose），
 *     供桌面动态卡片后台渐进式填充；inspect 不增 refcount，元数据拷贝出 .so 后即释放
 *
 * 锁粒度（双层）：
 *   - m_mutex 只保护 m_modules map 结构（查表/插入/删除/refcount，均微秒级短操作）
 *   - 慢操作（dlopen/dlclose/inspect）在 worker 上执行，不持 map 锁
 *   - 加载/卸载复用单飞 worker（内部 RAM 栈 + 二进制信号量，天然串行）
 */
#include <cstdint>
#include <cstddef>
#include <time.h>
#include <map>
#include <string>
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

    /** AppStack 在 delete App 之后调用（参数为 App 记录的模块路径）；refcount-- → 0 时 dlclose */
    void onAppDestroyed(const char* fullPath);

    /** 强制卸载；refcount>0 时拒绝并返回 false */
    bool unload(const char* fullPath);

    // ── 元数据扫描（桌面动态卡片） ──

    /** 扫描元数据（dlopen→读 info→dlclose，不增 refcount）；失败返回 false */
    struct ScanEntry
    {
        char path[64] = {};        // 完整路径（/root/elf/x.so）
        char name[32] = {};        // info->name（ASCII 键）
        char displayName[64] = {}; // info->display_name（回退 name）
        char desc[128] = {};       // info->description（可空）
        char iconPath[64] = {};    // info->icon_path（可空）
        size_t size = 0;
        time_t mtime = 0;
    };
    /** 快：列出 /root/elf 目录下的 .so 完整路径（不 dlopen） */
    size_t listElfFiles(char paths[][64], size_t max);
    /** 慢：走 worker dlopen→读元数据→dlclose；调用方在后台 Task 串行调用 */
    bool inspectModule(const char* fullPath, ScanEntry* out);

    // ── 状态查询（按 path；TestApp 面板用） ──
    bool isLoaded(const char* fullPath) const;
    const char* name(const char* fullPath) const;   // 插件名（info->name；仅 isLoaded 时有效）
    uint32_t version(const char* fullPath) const;
    uint32_t refcount(const char* fullPath) const;
    uint32_t loadTimeMs(const char* fullPath) const;
    size_t moduleSize(const char* fullPath) const;  // 文件大小

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
        enum class Op { Load, Inspect };
        char path[64] = {};
        Op op = Op::Load;
        bool ok = false;
        uint32_t loadTimeMs = 0;
        void* handle = nullptr;
        const struct elf_plugin_info* info = nullptr;
        ScanEntry scan;            // Inspect 结果（worker 内拷贝，dlclose 后仍有效）
    };

    // ── 内部 RAM 加载 worker（持久任务） ──

    void ensureWorker();                          // 首次创建 worker
    static void loadWorker(void* param);          // 持久 worker 主循环
    static void loadOne(LoadReq* req);            // Load: dlopen + dlsym + 版本校验
    static void inspectOne(LoadReq* req);         // Inspect: dlopen→拷贝元数据→dlclose

    static StackType_t s_loadStack[4096];         // 内部 RAM 静态栈（勿加 EXT_RAM_BSS_ATTR）
    static StaticTask_t s_loadTcb;

    // ── 工具 ──
    static void relativePath(const char* fullPath, char* out, size_t n); // "/root/elf/x.so" → "elf/x.so"
    static bool statFile(const char* fullPath, size_t* size, time_t* mtime); // 失败返回 true

    // path → Module（单表多模块；m_mutex 保护结构）
    std::map<std::string, Module> m_modules;
    mutable Mutex m_mutex;

    // 单次加载/检查请求（单飞：加载与扫描共用同一 worker，天然串行）
    LoadReq m_req;
    SemaphoreHandle_t m_reqSem = nullptr;
    volatile bool m_reqDone = false;
};
