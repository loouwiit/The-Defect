#pragma once
#include "esp_lv_adapter.h"
#include <cstdint>

class FontLoader
{
public:
	enum class FontSize : uint16_t
	{
		Small = 24,
		Medium = 32,
		Large = 56,

		Default = Medium,
	};

	static const lv_font_t* load(const char* vfsPath, uint16_t size);

	static const lv_font_t* getDefault(FontSize size = FontSize::Default);
	static bool setDefault(const lv_font_t* font, FontSize size = FontSize::Default);

private:
	FontLoader() = delete;
	~FontLoader() = delete;

	struct FontEntry
	{
		FontSize size{};
		const lv_font_t* font{};
	};

	static constexpr size_t MaxCount = 3;
	EXT_RAM_BSS_ATTR static FontEntry s_fonts[MaxCount];
};
