/*
 * C 符号导出表（R1 手工最小集，smoke 插件用）。
 * 完整表由 tools/gen_elf_syms.py 从主固件 .elf 生成后替换此文件：
 *   python3 tools/gen_elf_syms.py build/The-Defect.elf > main/elfSdk/elf_export_syms.c
 *
 * 说明：C 函数 name==symbol，可直接 ESP_ELFSYM_EXPORT；注册后 .so 的 lv_* /esp_log_*
 * /math 等 UND 符号运行时解析到主固件。
 */
#include "esp_elf.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include <math.h>
#include "esp_log.h"

const struct esp_elfsym g_elf_export_syms[] = {
    /* ── LVGL 基础（smoke 渲染所需的最小集） ── */
    ESP_ELFSYM_EXPORT(lv_obj_create),
    ESP_ELFSYM_EXPORT(lv_obj_delete),
    ESP_ELFSYM_EXPORT(lv_obj_set_size),
    ESP_ELFSYM_EXPORT(lv_obj_set_pos),
    ESP_ELFSYM_EXPORT(lv_obj_align),
    ESP_ELFSYM_EXPORT(lv_obj_center),
    ESP_ELFSYM_EXPORT(lv_color_hex),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_bg_opa),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_color),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_text_font),
    ESP_ELFSYM_EXPORT(lv_obj_add_flag),
    ESP_ELFSYM_EXPORT(lv_obj_clear_flag),
    ESP_ELFSYM_EXPORT(lv_obj_remove_style_all),
    ESP_ELFSYM_EXPORT(lv_label_create),
    ESP_ELFSYM_EXPORT(lv_label_set_text),
    ESP_ELFSYM_EXPORT(lv_label_set_text_fmt),
    ESP_ELFSYM_EXPORT(lv_screen_active),
    ESP_ELFSYM_EXPORT(lv_display_get_vertical_resolution),
    ESP_ELFSYM_EXPORT(lv_display_get_horizontal_resolution),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_radius),
    ESP_ELFSYM_EXPORT(lv_obj_set_style_pad_all),

    /* ── LVGL 适配器锁（Display::lockGuard 内联展开后引用） ── */
    ESP_ELFSYM_EXPORT(esp_lv_adapter_lock),
    ESP_ELFSYM_EXPORT(esp_lv_adapter_unlock),

    /* ── stdio/libc（loader 内置表无 snprintf/memcmp，插件必需） ── */
    ESP_ELFSYM_EXPORT(snprintf),
    ESP_ELFSYM_EXPORT(vsnprintf),
    ESP_ELFSYM_EXPORT(memcmp),

    /* ── 日志（ESP_LOGI 展开引用；含 App 内联虚函数里的 ESP_LOGI） ── */
    ESP_ELFSYM_EXPORT(esp_log_write),
    ESP_ELFSYM_EXPORT(esp_log),
    ESP_ELFSYM_EXPORT(esp_log_timestamp),

    /* ── math（游戏逻辑常用） ── */
    ESP_ELFSYM_EXPORT(atan2f),
    ESP_ELFSYM_EXPORT(sqrtf),
    ESP_ELFSYM_EXPORT(ceilf),
    ESP_ELFSYM_EXPORT(floorf),
    ESP_ELFSYM_EXPORT(fabsf),

    ESP_ELFSYM_END
};
