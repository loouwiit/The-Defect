/*
 * ElfManager — 见 elfManager.hpp。加载 ELF 必须在内部 RAM 栈任务（esp_elf_arch_flush
 * 期间 spi_flash_disable_interrupts_caches_and_other_cpu 要求栈在内部 RAM）。
 */
#include "elfManager.hpp"
#include "elfSdk/elf_sdk.h"
#include "esp_dlfcn.h"
#include "esp_timer.h"
#include "app/app.hpp"
#include <string.h>
#include <sys/stat.h>
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

/* ── 内部 RAM 加载 worker ─────────────────────────────── */

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

    ESP_LOGI(TAG, "dlopen OK: name=%s version=%u create_app=%p",
             info->name ? info->name : "?", info->version, (void*)info->create_app);

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

    Lock lock(m_mutex);

    // 已加载同路径且文件未变化 → 直接返回
    if (m_loaded && strcmp(m_module.path, fullPath) == 0)
    {
        size_t size = 0;
        time_t mtime = 0;
        bool missing = statFile(fullPath, &size, &mtime);
        if (!missing && size == m_module.size && mtime == m_module.mtime)
        {
            ESP_LOGI(TAG, "已加载 %s (refcount=%d)", m_module.path, m_module.refcount);
            return true;
        }
        // 文件变化（热更新）：refcount==0 才重载
        if (m_module.refcount > 0)
        {
            ESP_LOGW(TAG, "热更新延迟：模块仍有 %d 个 App 存活", m_module.refcount);
            return true;
        }
        ESP_LOGI(TAG, "检测到 %s 变化，重载", fullPath);
        if (m_module.handle)
            dlclose(m_module.handle);
        m_module = Module{};
        m_loaded = false;
    }

    // 启动内部 RAM worker（首次）
    if (!m_reqSem)
    {
        m_reqSem = xSemaphoreCreateBinary();
        m_reqDone = false;
        xTaskCreateStatic(loadWorker, "elfLoad", 4096, this, 5, s_loadStack, &s_loadTcb);
    }

    // 提交加载请求并等待
    m_req.path = fullPath;
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
    m_module.handle = m_req.handle;
    m_module.info = m_req.info;
    m_module.refcount = 0;
    m_module.loadTimeMs = m_req.loadTimeMs;
    statFile(fullPath, &m_module.size, &m_module.mtime);
    snprintf(m_module.path, sizeof(m_module.path), "%s", fullPath);
    m_loaded = true;

    ESP_LOGI(TAG, "%s 加载成功 耗时=%lums 大小=%u",
             m_module.path, (unsigned long)m_module.loadTimeMs, (unsigned)m_module.size);
    return true;
}

App* ElfManager::createApp(const char* fullPath, Display* display, const cJSON* params)
{
    if (!ensureLoaded(fullPath))
        return nullptr;

    Lock lock(m_mutex);
    if (!m_loaded || !m_module.info || !m_module.info->create_app)
        return nullptr;

    void* appPtr = m_module.info->create_app(display, params);
    if (!appPtr)
    {
        ESP_LOGE(TAG, "create_app 返回 null");
        return nullptr;
    }

    auto* app = (App*)appPtr;
    app->setElfModule(&m_module);
    m_module.refcount++;
    ESP_LOGI(TAG, "createApp OK: app=%p refcount=%d", (void*)app, m_module.refcount);
    return app;
}

void ElfManager::onAppDestroyed(void* module)
{
    Lock lock(m_mutex);
    if (module != &m_module)
        return;
    if (m_module.refcount > 0)
        m_module.refcount--;

    ESP_LOGI(TAG, "App 销毁，refcount=%d", m_module.refcount);

    if (m_module.refcount <= 0 && m_loaded && m_module.handle)
    {
        ESP_LOGI(TAG, "dlclose(%s)", m_module.path);
        dlclose(m_module.handle);
        m_module = Module{};
        m_loaded = false;
    }
}

bool ElfManager::unload(const char* fullPath)
{
    Lock lock(m_mutex);
    if (!m_loaded)
        return true;   // 已卸载
    if (strcmp(m_module.path, fullPath) != 0)
        return false;  // 路径不符
    if (m_module.refcount > 0)
    {
        ESP_LOGW(TAG, "拒绝卸载：refcount=%d", m_module.refcount);
        return false;
    }
    if (m_module.handle)
        dlclose(m_module.handle);
    m_module = Module{};
    m_loaded = false;
    ESP_LOGI(TAG, "已卸载 %s", fullPath);
    return true;
}

/* ── 状态查询 ─────────────────────────────────────────── */

bool ElfManager::isLoaded() const
{
    Lock lock(m_mutex);
    return m_loaded;
}

const char* ElfManager::path() const
{
    Lock lock(m_mutex);
    return m_module.path;
}

const char* ElfManager::name() const
{
    Lock lock(m_mutex);
    return (m_module.info && m_module.info->name) ? m_module.info->name : "";
}

uint32_t ElfManager::version() const
{
    Lock lock(m_mutex);
    return m_module.info ? m_module.info->version : 0;
}

uint32_t ElfManager::refcount() const
{
    Lock lock(m_mutex);
    return (uint32_t)m_module.refcount;
}

uint32_t ElfManager::loadTimeMs() const
{
    Lock lock(m_mutex);
    return m_module.loadTimeMs;
}

size_t ElfManager::moduleSize() const
{
    Lock lock(m_mutex);
    return m_module.size;
}
