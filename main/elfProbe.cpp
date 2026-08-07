/*
 * Phase 0 临时验证模块：主固件 dlopen/dlsym 加载 /root/hello.so。
 * ⚠ 关键：加载 ELF 的任务栈必须位于内部 RAM —— esp_elf_arch_flush 在 PSRAM
 * 加载时调用 spi_flash_disable_interrupts_caches_and_other_cpu()，期间访问
 * PSRAM 栈会触发 `esp_task_stack_is_sane_cache_disabled()` assert。
 * 因此不使用 Task::addTask（其 Thread 栈经 new 分配在 PSRAM 主堆），
 * 而是用 xTaskCreateStatic + 内部 RAM 静态栈（普通 static 数组在 .bss，
 * 勿加 EXT_RAM_BSS_ATTR）。
 */
#include "elfProbe.hpp"
#include "esp_dlfcn.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr char TAG[] = "elfProbe";

// 内部 RAM 静态栈（4096 words ≈ 16KB）
static StackType_t s_elfProbeStack[4096];
static StaticTask_t s_elfProbeTcb;

static void elfProbeTaskFunc(void* param)
{
    (void)param;

    // 延迟 10s，确保 FAT 已挂载 + .so 已上传就位
    vTaskDelay(pdMS_TO_TICKS(10000));

    ESP_LOGI(TAG, "--- dlopen test start ---");

    void* handle = dlopen("hello.so", RTLD_NOW);
    if (!handle) {
        ESP_LOGE(TAG, "dlopen failed: %s", dlerror());
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "dlopen OK, handle=%p", handle);

    int (*hello_world)(void) = (int (*)(void))dlsym(handle, "hello_world");
    if (!hello_world) {
        ESP_LOGE(TAG, "dlsym failed: %s", dlerror());
        dlclose(handle);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "dlsym(hello_world) OK, addr=%p", (void*)hello_world);

    int ret = hello_world();
    ESP_LOGI(TAG, "hello_world() returned %d", ret);

    ESP_LOGI(TAG, "--- dlopen test done ---");
    vTaskDelete(NULL);
}

void elfProbeStart()
{
    xTaskCreateStatic(elfProbeTaskFunc, "elfProbe",
                      4096, nullptr, 5, s_elfProbeStack, &s_elfProbeTcb);
}
