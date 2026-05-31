#pragma once
#include "esp_lv_adapter.h"

class FontLoader {
public:
	static const lv_font_t* load(const char* vfsPath);

	static const lv_font_t* getDefault();
	static void setDefault(const lv_font_t* font);

private:
	FontLoader() = delete;
	~FontLoader() = delete;

	EXT_RAM_BSS_ATTR static const lv_font_t* s_defaultFont;
};
