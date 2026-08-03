#pragma once

#include "lvgl.h"

/**
 * @brief 自定义拼音词库（从 FAT 加载，替换 LVGL 内置 101 条繁体残缺词库）
 *
 * 词库文件: /root/system/pinyin_dict.txt
 *   - 由 tools/gen_pinyin_dict.py 从 Rime《通用规范汉字表》8105 字表生成
 *   - 格式: 拼音<TAB>汉字直接连接（每字 3 字节 UTF-8，无分隔符）
 *   - 已按字典序排列（LVGL 前缀匹配要求），组内按字频降序（常用靠前）
 *
 * 使用（创建 IME 后注入）：
 *   lv_ime_pinyin_set_dict(ime, const_cast<lv_pinyin_dict_t*>(PinyinDict::loadFromFat()));
 *   // 返回 nullptr 时保持 LVGL 内置词库（回退）
 */
class PinyinDict {
public:
    /** @brief 加载拼音词库（/root/system/pinyin_dict.txt，单例缓存）。
     *  返回以 {NULL,NULL} 终止的 lv_pinyin_dict_t 数组（PSRAM）；失败返回 nullptr */
    static const lv_pinyin_dict_t* loadFromFat();

    /** @brief 加载五笔词库（/root/system/wubi_dict.txt，单例缓存）。失败返回 nullptr */
    static const lv_pinyin_dict_t* loadWubiFromFat();

    /** @brief 加载混合词库（五笔优先+拼音兜底，/root/system/wubi_pinyin_dict.txt）。失败返回 nullptr */
    static const lv_pinyin_dict_t* loadMixedFromFat();

private:
    static const lv_pinyin_dict_t* doLoad(const char* path);
};
