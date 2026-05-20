#pragma once

#include "lvgl.h"
#include <cstdint>

class GUI {
public:
	struct Color {
		static constexpr lv_color_t BG      = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
		static constexpr lv_color_t CARD    = LV_COLOR_MAKE(0x2d, 0x2d, 0x3d);
		static constexpr lv_color_t PRIMARY = LV_COLOR_MAKE(0x00, 0x88, 0xff);
		static constexpr lv_color_t SUCCESS = LV_COLOR_MAKE(0x00, 0xc8, 0x53);
		static constexpr lv_color_t WARNING = LV_COLOR_MAKE(0xff, 0xa8, 0x00);
		static constexpr lv_color_t DANGER  = LV_COLOR_MAKE(0xff, 0x3b, 0x30);
		static constexpr lv_color_t TEXT    = LV_COLOR_MAKE(0xff, 0xff, 0xff);
		static constexpr lv_color_t SUBTLE  = LV_COLOR_MAKE(0x88, 0x88, 0x88);
	};


	static void setBackground(lv_color_t color = Color::BG);

	static lv_obj_t* createPage();

	static lv_obj_t* createCard(lv_obj_t* parent, int32_t w, int32_t h);

	static lv_obj_t* createFlex(lv_obj_t* parent, lv_flex_flow_t flow,
	                            int32_t w = LV_SIZE_CONTENT, int32_t h = LV_SIZE_CONTENT);


	static lv_obj_t* createButton(lv_obj_t* parent, const char* text,
	                              int32_t w = 120, int32_t h = 40);

	static lv_obj_t* createTitle(lv_obj_t* parent, const char* text);

	static lv_obj_t* createSubtitle(lv_obj_t* parent, const char* text);

	static lv_obj_t* createValue(lv_obj_t* parent, const char* text);

	static lv_obj_t* createLabel(lv_obj_t* parent, const char* text);

	static lv_obj_t* createSwitch(lv_obj_t* parent);

	static lv_obj_t* createSlider(lv_obj_t* parent, int32_t w,
	                              int32_t min, int32_t max, int32_t init);

	static lv_obj_t* createProgressBar(lv_obj_t* parent, int32_t w,
	                                   int32_t min, int32_t max, int32_t init);


	static lv_obj_t* createMetric(lv_obj_t* parent, const char* title,
	                              const char* value, const char* unit);

	static lv_obj_t* createProgressCard(lv_obj_t* parent, const char* title,
	                                    int32_t min, int32_t max, int32_t init);

	static lv_obj_t* createMenuRow(lv_obj_t* parent, const char* text,
	                               lv_obj_t* rightWidget = nullptr);


	static lv_obj_t* createImage(lv_obj_t* parent, const void* src);

	static lv_obj_t* createImageFromFile(lv_obj_t* parent, const char* path);
};

static inline void styleCard(lv_obj_t* obj)
{
	lv_obj_set_style_bg_color(obj, GUI::Color::CARD, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 12, 0);
	lv_obj_set_style_shadow_width(obj, 8, 0);
	lv_obj_set_style_shadow_color(obj, lv_color_hex(0x000000), 0);
	lv_obj_set_style_shadow_opa(obj, LV_OPA_50, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 12, 0);
}
