/*
 * ElfManager — 见 elfManager.hpp。加载 ELF 必须在内部 RAM 栈任务（esp_elf_arch_flush
 * 期间 spi_flash_disable_interrupts_caches_and_other_cpu 要求栈在内部 RAM）。
 */
#include "elfManager.hpp"
#include "elfSdk/elf_sdk.h"
#include "esp_dlfcn.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "app/app.hpp"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "esp_log.h"

static constexpr char TAG[] = "ElfManager";

/* 内部 RAM 静态栈（普通 static 数组在 .bss，勿加 EXT_RAM_BSS_ATTR） */
StackType_t ElfManager::s_loadStack[4096];
StaticTask_t ElfManager::s_loadTcb;

ElfManager& ElfManager::instance()
{
    static ElfManager s;
    return s;
}

/* ── 工具 ─────────────────────────────────────────────── */

void ElfManager::relativePath(const char* fullPath, char* out, size_t n)
{
    // dlopen 的 esp_elf_open 会拼 FS_PATH/name，故传相对路径；输入默认带 /root/ 前缀
    const char* rel = fullPath;
    if (strncmp(fullPath, "/root/", 6) == 0)
        rel = fullPath + 6;
    snprintf(out, n, "%s", rel);
}

bool ElfManager::statFile(const char* fullPath, size_t* size, time_t* mtime)
{
    struct stat st;
    if (stat(fullPath, &st) != 0)
        return true;   // 文件不存在/读取失败
    if (size)
        *size = (size_t)st.st_size;
    if (mtime)
        *mtime = st.st_mtime;
    return false;
}

/* ── 内部 RAM 加载 worker（持久任务） ─────────────────── */

void ElfManager::ensureWorker()
{
    if (!m_reqSem)
    {
        m_reqSem = xSemaphoreCreateBinary();
        m_reqDone = false;
        xTaskCreateStatic(loadWorker, "elfLoad", 4096, this, 5, s_loadStack, &s_loadTcb);
    }
}

void ElfManager::loadOne(LoadReq* req)
{
    char rel[64];
    relativePath(req->path, rel, sizeof(rel));

    ESP_LOGI(TAG, "dlopen(%s)", rel);
    void* handle = dlopen(rel, RTLD_NOW);
    if (!handle)
    {
        ESP_LOGE(TAG, "dlopen failed: %s", dlerror());
        return;
    }

    void* sym = dlsym(handle, "elf_plugin_entry");
    if (!sym)
    {
        ESP_LOGE(TAG, "dlsym(elf_plugin_entry) failed: %s", dlerror());
        dlclose(handle);
        return;
    }

    // ⚠ dlsym 返回的是 elf_plugin_entry 的【函数指针】，必须调用它拿到插件信息结构
    // （旧代码直接 cast 成结构体指针解引用 version → 读到函数机器码，版本=1303 等垃圾值）
    typedef const elf_plugin_info_t* (*elf_entry_fn_t)(void);
    auto* entry = (elf_entry_fn_t)sym;
    const elf_plugin_info_t* info = entry();
    if (!info)
    {
        ESP_LOGE(TAG, "elf_plugin_entry() 返回 null");
        dlclose(handle);
        return;
    }

    if (info->version != ELF_SDK_VERSION)
    {
        ESP_LOGE(TAG, "版本不匹配: 插件=%u 主固件=%u", info->version, ELF_SDK_VERSION);
        dlclose(handle);
        return;
    }

    if (!info->create_app)
    {
        ESP_LOGE(TAG, "插件缺少 create_app");
        dlclose(handle);
        return;
    }

    ESP_LOGI(TAG, "dlopen OK: version=%u create_app=%p",
             info->version, (void*)info->create_app);

    req->ok = true;
    req->handle = handle;
    req->info = info;
}

void ElfManager::loadWorker(void* param)
{
    auto* self = (ElfManager*)param;
    for (;;)
    {
        if (xSemaphoreTake(self->m_reqSem, portMAX_DELAY) != pdTRUE)
            continue;

        LoadReq* req = &self->m_req;
        req->ok = false;
        req->handle = nullptr;
        req->info = nullptr;

        uint32_t t0 = esp_timer_get_time() / 1000;
        loadOne(req);
        req->loadTimeMs = (uint32_t)(esp_timer_get_time() / 1000 - t0);

        self->m_reqDone = true;
    }
}

/* ── 对外接口 ─────────────────────────────────────────── */

bool ElfManager::ensureLoaded(const char* fullPath)
{
    if (!fullPath || !*fullPath)
        return false;

    std::string key(fullPath);
    Lock lock(m_mutex);

    // 已加载同路径且文件未变化 → 直接返回
    auto it = m_modules.find(key);
    if (it != m_modules.end())
    {
        Module& m = it->second;
        size_t size = 0;
        time_t mtime = 0;
        bool missing = statFile(fullPath, &size, &mtime);
        if (!missing && size == m.size && mtime == m.mtime)
        {
            ESP_LOGI(TAG, "已加载 %s (refcount=%d)", m.path, m.refcount);
            return true;
        }
        // 文件变化（热更新）：refcount==0 才重载
        if (m.refcount > 0)
        {
            ESP_LOGW(TAG, "热更新延迟：模块 %s 仍有 %d 个 App 存活", m.path, m.refcount);
            return true;
        }
        ESP_LOGI(TAG, "检测到 %s 变化，重载", fullPath);
        if (m.handle)
            dlclose(m.handle);
        m_modules.erase(it);
    }

    ensureWorker();

    // 提交加载请求并等待（加载期间持 map 锁可接受：加载天然全局串行）
    m_req.op = LoadReq::Op::Load;
    snprintf(m_req.path, sizeof(m_req.path), "%s", fullPath);
    m_reqDone = false;
    xSemaphoreGive(m_reqSem);
    while (!m_reqDone)
        vTaskDelay(pdMS_TO_TICKS(1));

    if (!m_req.ok)
    {
        ESP_LOGE(TAG, "加载失败: %s", fullPath);
        return false;
    }

    // 缓存
    Module m;
    m.handle = m_req.handle;
    m.info = m_req.info;
    m.refcount = 0;
    m.loadTimeMs = m_req.loadTimeMs;
    statFile(fullPath, &m.size, &m.mtime);
    snprintf(m.path, sizeof(m.path), "%s", fullPath);
    m_modules[key] = m;

    ESP_LOGI(TAG, "%s 加载成功 耗时=%lums 大小=%u",
             m.path, (unsigned long)m.loadTimeMs, (unsigned)m.size);
    return true;
}

App* ElfManager::createApp(const char* fullPath, Display* display, const cJSON* params)
{
    if (!ensureLoaded(fullPath))
        return nullptr;

    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    if (it == m_modules.end() || !it->second.info || !it->second.info->create_app)
        return nullptr;

    void* appPtr = it->second.info->create_app(display, params);
    if (!appPtr)
    {
        ESP_LOGE(TAG, "create_app 返回 null");
        return nullptr;
    }

    auto* app = (App*)appPtr;
    app->setElfModule(fullPath);   // 存 path（map 节点地址不稳，不能用指针）
    it->second.refcount++;
    ESP_LOGI(TAG, "createApp OK: app=%p module=%s refcount=%d",
             (void*)app, it->second.path, it->second.refcount);
    return app;
}

void ElfManager::onAppDestroyed(const char* fullPath)
{
    if (!fullPath || !*fullPath)
        return;

    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    if (it == m_modules.end())
        return;

    Module& m = it->second;
    if (m.refcount > 0)
        m.refcount--;

    ESP_LOGI(TAG, "App 销毁，refcount=%d", m.refcount);

    if (m.refcount <= 0 && m.handle)
    {
        ESP_LOGI(TAG, "dlclose(%s)", m.path);
        dlclose(m.handle);
        m_modules.erase(it);
    }
}

bool ElfManager::unload(const char* fullPath)
{
    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    if (it == m_modules.end())
        return true;   // 已卸载
    if (it->second.refcount > 0)
    {
        ESP_LOGW(TAG, "拒绝卸载 %s：refcount=%d", it->second.path, it->second.refcount);
        return false;
    }
    if (it->second.handle)
        dlclose(it->second.handle);
    m_modules.erase(it);
    ESP_LOGI(TAG, "已卸载 %s", fullPath);
    return true;
}

/* ── 元数据扫描（桌面动态卡片；插件包 = /root/elf/<name>/{manifest.json, <name>.so, icon}） ── */

bool ElfManager::readManifest(const char* dirPath, ScanEntry* out)
{
    if (!dirPath || !*dirPath || !out)
        return false;

    // 读 /root/elf/<name>/manifest.json（FAT 小文件）
    char path[96];
    snprintf(path, sizeof(path), "%s/manifest.json", dirPath);

    FILE* f = fopen(path, "r");
    if (!f)
        return false;

    // 文件 ≤4KB，直接整读
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0)
        return false;
    buf[n] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root)
        return false;

    ScanEntry e = {};
    const char* mainName = nullptr;

    const cJSON* j;
    j = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(j) && j->valuestring[0])
        snprintf(e.name, sizeof(e.name), "%s", j->valuestring);
    j = cJSON_GetObjectItem(root, "display_name");
    if (cJSON_IsString(j) && j->valuestring[0])
        snprintf(e.displayName, sizeof(e.displayName), "%s", j->valuestring);
    j = cJSON_GetObjectItem(root, "description");
    if (cJSON_IsString(j) && j->valuestring[0])
        snprintf(e.desc, sizeof(e.desc), "%s", j->valuestring);
    j = cJSON_GetObjectItem(root, "icon");
    if (cJSON_IsString(j) && j->valuestring[0])
        snprintf(e.iconPath, sizeof(e.iconPath), "%s", j->valuestring);
    j = cJSON_GetObjectItem(root, "main");
    if (cJSON_IsString(j) && j->valuestring[0])
        mainName = j->valuestring;

    cJSON_Delete(root);

    if (!e.name[0])
        return false;   // 缺 name → 视为无效包

    // display_name 回退 name
    if (!e.displayName[0])
        snprintf(e.displayName, sizeof(e.displayName), "%s", e.name);

    // .so 完整路径：/root/elf/<name>/<main>；main 缺省回退 "<目录名>.so"
    if (!mainName || !*mainName)
    {
        const char* base = strrchr(dirPath, '/');
        base = base ? base + 1 : dirPath;
        snprintf(e.path, sizeof(e.path), "%s/%s.so", dirPath, base);
    }
    else
    {
        snprintf(e.path, sizeof(e.path), "%s/%s", dirPath, mainName);
    }

    *out = e;
    return true;
}

size_t ElfManager::listElfApps(ScanEntry* entries, size_t max)
{
    if (!entries || max == 0)
        return 0;

    DIR* dir = opendir("/root/elf");
    if (!dir)
        return 0;

    size_t count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && count < max)
    {
        // 只收子目录（插件包目录）；跳过 . ..
        if (ent->d_type != DT_DIR)
            continue;
        const char* n = ent->d_name;
        if (n[0] == '.')
            continue;

        char dirPath[96];
        // 手动拼接（GCC -Wformat-truncation 对 dirent 名最长 255 字节的推断会误报）
        static const char kPrefix[] = "/root/elf/";   // 10 字节（不含 null）
        constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
        size_t nameLen = strlen(n);
        size_t copyLen = nameLen < sizeof(dirPath) - kPrefixLen - 1
                             ? nameLen : sizeof(dirPath) - kPrefixLen - 1;
        memcpy(dirPath, kPrefix, kPrefixLen);
        memcpy(dirPath + kPrefixLen, n, copyLen);
        dirPath[kPrefixLen + copyLen] = '\0';
        if (readManifest(dirPath, &entries[count]))
            count++;
    }
    closedir(dir);

    // 排序（目录读序不定，按 .so 路径排序保证桌面顺序稳定）
    for (size_t i = 0; i < count; i++)
        for (size_t j = i + 1; j < count; j++)
            if (strcmp(entries[j].path, entries[i].path) < 0)
            {
                ScanEntry t = entries[i];
                entries[i] = entries[j];
                entries[j] = t;
            }

    return count;
}

/* ── 状态查询 ─────────────────────────────────────────── */

bool ElfManager::isLoaded(const char* fullPath) const
{
    if (!fullPath || !*fullPath)
        return false;
    Lock lock(m_mutex);
    return m_modules.find(fullPath) != m_modules.end();
}

uint32_t ElfManager::version(const char* fullPath) const
{
    if (!fullPath || !*fullPath)
        return 0;
    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    return (it != m_modules.end() && it->second.info) ? it->second.info->version : 0;
}

uint32_t ElfManager::refcount(const char* fullPath) const
{
    if (!fullPath || !*fullPath)
        return 0;
    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    return (it != m_modules.end()) ? (uint32_t)it->second.refcount : 0;
}

uint32_t ElfManager::loadTimeMs(const char* fullPath) const
{
    if (!fullPath || !*fullPath)
        return 0;
    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    return (it != m_modules.end()) ? it->second.loadTimeMs : 0;
}

size_t ElfManager::moduleSize(const char* fullPath) const
{
    if (!fullPath || !*fullPath)
        return 0;
    Lock lock(m_mutex);
    auto it = m_modules.find(fullPath);
    return (it != m_modules.end()) ? it->second.size : 0;
}
