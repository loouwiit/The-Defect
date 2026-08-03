#include "pinyinDict.hpp"

#include "esp_heap_caps.h"
#include <esp_log.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

static constexpr char TAG[] = "PinyinDict";

// 词库文件路径（FAT 分区，reserces/system/ 上传到 /root/）
static const char* PINYIN_PATH = "/root/system/pinyin_dict.txt";
static const char* WUBI_PATH   = "/root/system/wubi_dict.txt";

namespace {

struct Line {
    std::string py;
    std::string mb;
};

} // namespace

const lv_pinyin_dict_t* PinyinDict::loadFromFat()
{
    // C++11 magic static：线程安全，仅首次初始化
    static const lv_pinyin_dict_t* s_dict = doLoad(PINYIN_PATH);
    return s_dict;
}

const lv_pinyin_dict_t* PinyinDict::loadWubiFromFat()
{
    static const lv_pinyin_dict_t* s_dict = doLoad(WUBI_PATH);
    return s_dict;
}

const lv_pinyin_dict_t* PinyinDict::doLoad(const char* dictPath)
{
    FILE* f = fopen(dictPath, "r");
    if (!f) {
        ESP_LOGW(TAG, "无法打开 %s，回退 LVGL 内置词库", dictPath);
        return nullptr;
    }

    // ── 1. 解析：每行 拼音<TAB>汉字串（无分隔符，每字 3 字节 UTF-8） ──
    std::vector<Line> lines;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        if (len == 0) continue;

        char* tab = strchr(buf, '\t');
        if (!tab) continue;             // 非法行跳过
        *tab = '\0';
        const char* py = buf;
        const char* mb = tab + 1;

        // 校验拼音（仅小写字母）
        const char* p = py;
        if (*p == '\0') continue;
        bool ok = true;
        for (; *p; p++) {
            if (*p < 'a' || *p > 'z') { ok = false; break; }
        }
        if (!ok) continue;

        // 校验汉字串为 3 字节倍数
        size_t mbLen = strlen(mb);
        if (mbLen == 0 || mbLen % 3 != 0) {
            ESP_LOGW(TAG, "跳过非法行: 拼音=%s 汉字长度=%zu（非 3 倍数）", py, mbLen);
            continue;
        }

        lines.push_back({ py, mb });
    }
    fclose(f);

    if (lines.empty()) {
        ESP_LOGW(TAG, "词库无有效条目，回退 LVGL 内置词库");
        return nullptr;
    }

    // ── 2. 按拼音字典序排序（LVGL 前缀匹配要求，容忍源文件乱序） ──
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) {
        return strcmp(a.py.c_str(), b.py.c_str()) < 0;
    });

    // ── 3. 紧凑分配 PSRAM（entries 数组 + 字符串区，单块） ──
    size_t n = lines.size();
    size_t strSize = 0;
    for (const auto& l : lines)
        strSize += l.py.size() + 1 + l.mb.size() + 1;
    size_t entriesSize = (n + 1) * sizeof(lv_pinyin_dict_t);
    size_t total = entriesSize + strSize;

    auto* base = static_cast<char*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM));
    if (!base) {
        ESP_LOGE(TAG, "PSRAM 分配失败（%zu 字节），回退 LVGL 内置词库", total);
        return nullptr;
    }

    // lv_pinyin_dict_t 的成员是 const 指针（const char * const），无法在运行时赋值。
    // 用布局一致（两个指针）的可写结构填充，再 reinterpret_cast 回 lv_pinyin_dict_t。
    // LVGL 仅只读访问该数组（init_pinyin_dict / pinyin_search_matching），安全。
    struct WritableEntry {
        const char* py;
        const char* py_mb;
    };
    auto* we = reinterpret_cast<WritableEntry*>(base);
    char* strPool = base + entriesSize;
    for (size_t i = 0; i < n; i++) {
        char* pyDst = strPool;
        strcpy(pyDst, lines[i].py.c_str());
        strPool += lines[i].py.size() + 1;

        char* mbDst = strPool;
        strcpy(mbDst, lines[i].mb.c_str());
        strPool += lines[i].mb.size() + 1;

        we[i].py    = pyDst;
        we[i].py_mb = mbDst;
    }
    we[n].py    = nullptr;
    we[n].py_mb = nullptr;

    auto* entries = reinterpret_cast<lv_pinyin_dict_t*>(base);

    ESP_LOGI(TAG, "词库加载成功: %zu 个拼音条目，%zu 字节 PSRAM", n, total);
    return entries;
}
