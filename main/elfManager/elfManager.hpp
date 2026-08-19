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
 *   - 元数据扫描：listElfApps（快，只读目录 + manifest.json，零 dlopen），
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

    // ── 元数据扫描（桌面动态卡片；插件包 = /root/elf/<name>/{manifest.json, <name>.so, icon}） ──

    /** 插件元数据（来自 manifest.json，读 FAT + cJSON，零 dlopen） */
    struct ScanEntry
    {
        char path[64] = {};        // .so 完整路径（/root/elf/<name>/<name>.so，ensureLoaded/createApp 用）
        char name[32] = {};        // manifest.name（ASCII，AI open_app 键）
        char displayName[64] = {}; // manifest.display_name
        char desc[128] = {};       // manifest.description（可空）
        char iconPath[64] = {};    // manifest.icon（F: 前缀，LVGL 直接加载；可空）
    };
    /** 读单个插件包的 manifest.json；dirPath 形如 "/root/elf/tetris"；失败返回 false */
    bool readManifest(const char* dirPath, ScanEntry* out);
    /** 快：扫描 /root/elf 子目录，逐个读 manifest.json 填 ScanEntry；返回有效插件数 */
    size_t listElfApps(ScanEntry* entries, size_t max);

    // ── 状态查询（按 path；TestApp 面板用） ──
    bool isLoaded(const char* fullPath) const;
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
        enum class Op { Load };
        char path[64] = {};
        Op op = Op::Load;
        bool ok = false;
        uint32_t loadTimeMs = 0;
        void* handle = nullptr;
        const struct elf_plugin_info* info = nullptr;
    };

    // ── 内部 RAM 加载 worker（持久任务） ──

    void ensureWorker();                          // 首次创建 worker
    static void loadWorker(void* param);          // 持久 worker 主循环
    static void loadOne(LoadReq* req);            // Load: dlopen + dlsym + 版本校验

    static StackType_t s_loadStack[4096];         // 内部 RAM 静态栈（勿加 EXT_RAM_BSS_ATTR）
    static StaticTask_t s_loadTcb;

    // ── 工具 ──
    static void relativePath(const char* fullPath, char* out, size_t n); // "/root/elf/x.so" → "elf/x.so"
    // 查询插件文件状态：返回 true=文件缺失，false=存在（size/mtime 输出，热更新检测用）
    static bool statFile(const char* fullPath, size_t* size, time_t* mtime);

    // path → Module（单表多模块；m_mutex 保护结构）
    std::map<std::string, Module> m_modules;
    mutable Mutex m_mutex;

    // 单次加载/检查请求（单飞：加载与扫描共用同一 worker，天然串行）
    LoadReq m_req;
    SemaphoreHandle_t m_reqSem = nullptr;
    volatile bool m_reqDone = false;
};
