/*
 * elf_sdk.cpp — ELF 插件 SDK：符号表注册 + 启动优化。
 *
 * 1. 符号表注册（原有）：elf_sdk_init() 在 main.cpp 启动早期调用，注册一张由脚本生成的符号表
 *    （g_main_elfsyms，见 elf_symtab.c）：tools/gen_elf_syms.py 从主固件 .elf 提取
 *    符号（nm 含 WEAK），按白名单过滤 C 函数 / C++ 类方法（单例直出）/ C++ 运行时，
 *    生成 { "name", (void*)&name } 表（&name 链接期取址）。
 *    .so include 主固件头文件直接调用（Xxx::instance().method() / Task::addTask 等原生风格）。
 *
 * 2. 启动日志静默：esp_elf.c / dlmod.c 用 ESP_LOGI(TAG="ELF"/"DLMOD") 打印大量加载细节
 *    （每次 dlopen 每个符号一行 symtab 打印，桌面扫描 5 插件 ~850+ 行）。elf_sdk_init()
 *    里一次性把这两个 tag 降到 WARN —— 错误级日志仍可见，无需 patch 组件。
 *
 * 3. 符号解析哈希化：默认 resolver（elf_find_sym_default）对 g_main_elfsyms（2133 符号）
 *    线性 strcmp —— 每次 dlopen 的每个 UND 符号都全表扫描（~1000+ 次比较）。
 *    这里构建 FNV-1a 哈希桶索引（PSRAM，name/sym 指针零拷贝直指符号表内），
 *    自定义 resolver 查哈希 O(1)，未命中兜底默认 resolver（libc/espidf 静态表 + dlmod 动态模块）。
 *    零组件 patch，esp_elf_loader 更新不失效。
 */
#include "elf_sdk.h"
#include "esp_elf.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

extern const struct esp_elfsym g_main_elfsyms[];

/* esp_elf_loader 的默认符号解析器（esp_elf_symbol.c，C linkage，非 static）。
 * 自定义 resolver 未命中时兜底：libc/espidf 静态表 + esp_elf_register_symbol 注册表
 * （含 g_main_elfsyms）+ 动态加载模块（dlmod_getaddr）。 */
extern "C" uintptr_t elf_find_sym_default(const char *sym_name);
/* 运行时替换内部符号解析器（esp_elf.c，C linkage） */
extern "C" void elf_set_symbol_resolver(uintptr_t (*resolver)(const char *sym_name));

/* ── 启动日志静默 ─────────────────────────────────────── */

static void elfSdkSilenceLoaderLogs(void)
{
    /* esp_elf.c: "ELF" — symtab 逐符号 / version / entry / padding 等 INFO */
    esp_log_level_set("ELF", ESP_LOG_WARN);
    /* dlmod.c: "DLMOD" — Removed node / 模块列表等 INFO */
    esp_log_level_set("DLMOD", ESP_LOG_WARN);
}

/* ── 符号解析哈希索引 ─────────────────────────────────── */

#define ELF_SDK_HASH_BUCKETS 2048   /* 2133 符号 → 平均负载 1.04 */
#define ELF_SDK_HASH_MASK   (ELF_SDK_HASH_BUCKETS - 1)

static_assert((ELF_SDK_HASH_BUCKETS & ELF_SDK_HASH_MASK) == 0, "桶数须为 2 的幂");

struct ElfSdkHashNode {
    const char* name;      /* 指向 g_main_elfsyms 内（零拷贝） */
    const void* sym;
    int32_t next;          /* 同桶链（-1=尾） */
};

EXT_RAM_BSS_ATTR static int32_t s_bucketHead[ELF_SDK_HASH_BUCKETS];
static ElfSdkHashNode* s_nodes = nullptr;   /* PSRAM，按实际符号数分配 */
static size_t s_nodeCount = 0;
static bool s_indexBuilt = false;

static uint32_t elfSdkHash(const char* s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static void elfSdkBuildIndex(void)
{
    /* 统计符号数 */
    size_t count = 0;
    for (const struct esp_elfsym* p = g_main_elfsyms; p->name; p++)
        count++;
    if (count == 0)
        return;

    s_nodes = (ElfSdkHashNode*)heap_caps_malloc(count * sizeof(ElfSdkHashNode), MALLOC_CAP_SPIRAM);
    if (!s_nodes)
    {
        /* PSRAM 分配失败 → 退回默认 resolver（线性查找，功能不变） */
        ESP_LOGW("elfSdk", "符号哈希索引分配失败，退回默认 resolver");
        return;
    }

    memset(s_bucketHead, 0xFF, sizeof(s_bucketHead));   /* -1 = 空桶 */

    for (const struct esp_elfsym* p = g_main_elfsyms; p->name; p++)
    {
        uint32_t h = elfSdkHash(p->name) & ELF_SDK_HASH_MASK;
        s_nodes[s_nodeCount] = { p->name, p->sym, s_bucketHead[h] };
        s_bucketHead[h] = (int32_t)s_nodeCount;
        s_nodeCount++;
    }
    s_indexBuilt = true;
}

static uintptr_t elfSdkResolve(const char* name)
{
    if (!name || !*name)
        return 0;

    if (s_indexBuilt)
    {
        uint32_t h = elfSdkHash(name) & ELF_SDK_HASH_MASK;
        for (int32_t i = s_bucketHead[h]; i >= 0; i = s_nodes[i].next)
        {
            if (strcmp(s_nodes[i].name, name) == 0)
                return (uintptr_t)s_nodes[i].sym;
        }
    }
    return elf_find_sym_default(name);
}

/* ── 注册 ─────────────────────────────────────────────── */

extern "C" void elf_sdk_init(void)
{
    /* 静默 ELF/DLMOD 组件 INFO 日志（在首个 dlopen 之前，启动早期） */
    elfSdkSilenceLoaderLogs();

    /* 哈希索引 + 自定义 resolver（esp_elf_register_symbol 与 resolver 替换均须
     * 在任意 dlopen 之前、单线程上下文调用） */
    elfSdkBuildIndex();
    elf_set_symbol_resolver(elfSdkResolve);

    esp_elf_register_symbol((esp_elf_symbol_table_t*)g_main_elfsyms);
}
