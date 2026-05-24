#include "gui.hpp"
#include "esp_log.h"

static const char* TAG = "GUI";


void GUI::setBackground(lv_color_t color)
{
	lv_obj_set_style_bg_color(lv_scr_act(), color, 0);
	lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

lv_obj_t* GUI::createPage()
{
	lv_obj_t* page = lv_obj_create(lv_scr_act());
	lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);
	lv_obj_set_style_border_width(page, 0, 0);
	lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(page, 16, 0);
	lv_obj_set_style_pad_top(page, 24, 0);
	lv_obj_set_style_pad_bottom(page, 24, 0);
	lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
	return page;
}


lv_obj_t* GUI::createCard(lv_obj_t* parent, int32_t w, int32_t h)
{
	lv_obj_t* card = lv_obj_create(parent);
	lv_obj_set_size(card, w, h);
	styleCard(card);
	return card;
}

lv_obj_t* GUI::createFlex(lv_obj_t* parent, lv_flex_flow_t flow,
                           int32_t w, int32_t h)
{
	lv_obj_t* cont = lv_obj_create(parent);
	lv_obj_set_size(cont, w, h);
	lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(cont, flow);
	lv_obj_set_style_pad_all(cont, 8, 0);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
	return cont;
}


lv_obj_t* GUI::createButton(lv_obj_t* parent, const char* text,
                             int32_t w, int32_t h)
{
	lv_obj_t* btn = lv_button_create(parent);
	lv_obj_set_size(btn, w, h);
	lv_obj_set_style_radius(btn, 8, 0);
	lv_obj_set_style_bg_color(btn, Color::PRIMARY, 0);

	lv_obj_t* label = lv_label_create(btn);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, Color::TEXT, 0);
	lv_obj_center(label);

	return btn;
}

lv_obj_t* GUI::createTitle(lv_obj_t* parent, const char* text)
{
	lv_obj_t* label = lv_label_create(parent);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(label, Color::TEXT, 0);
	return label;
}

lv_obj_t* GUI::createSubtitle(lv_obj_t* parent, const char* text)
{
	lv_obj_t* label = lv_label_create(parent);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(label, Color::SUBTLE, 0);
	return label;
}

lv_obj_t* GUI::createValue(lv_obj_t* parent, const char* text)
{
	lv_obj_t* label = lv_label_create(parent);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(label, Color::PRIMARY, 0);
	return label;
}

lv_obj_t* GUI::createLabel(lv_obj_t* parent, const char* text)
{
	lv_obj_t* label = lv_label_create(parent);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, Color::TEXT, 0);
	return label;
}

lv_obj_t* GUI::createSwitch(lv_obj_t* parent)
{
	lv_obj_t* sw = lv_switch_create(parent);
	return sw;
}

lv_obj_t* GUI::createSlider(lv_obj_t* parent, int32_t w,
                             int32_t min, int32_t max, int32_t init)
{
	lv_obj_t* slider = lv_slider_create(parent);
	lv_obj_set_width(slider, w);
	lv_slider_set_range(slider, min, max);
	lv_slider_set_value(slider, init, LV_ANIM_OFF);
	return slider;
}

lv_obj_t* GUI::createProgressBar(lv_obj_t* parent, int32_t w,
                                  int32_t min, int32_t max, int32_t init)
{
	lv_obj_t* bar = lv_bar_create(parent);
	lv_obj_set_width(bar, w);
	lv_bar_set_range(bar, min, max);
	lv_bar_set_value(bar, init, LV_ANIM_OFF);
	return bar;
}


lv_obj_t* GUI::createMetric(lv_obj_t* parent, const char* title,
                             const char* value, const char* unit)
{
	lv_obj_t* card = createCard(parent, 200, 120);
	lv_obj_set_layout(card, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
	                       LV_FLEX_ALIGN_CENTER);

	lv_obj_t* titleLabel = lv_label_create(card);
	lv_label_set_text(titleLabel, title);
	lv_obj_set_style_text_color(titleLabel, Color::SUBTLE, 0);
	lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_14, 0);

	lv_obj_t* row = lv_obj_create(card);
	lv_obj_set_width(row, lv_pct(100));
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(row, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
	                       LV_FLEX_ALIGN_CENTER);

	lv_obj_t* valLabel = lv_label_create(row);
	lv_label_set_text(valLabel, value);
	lv_obj_set_style_text_font(valLabel, &lv_font_montserrat_36, 0);
	lv_obj_set_style_text_color(valLabel, Color::TEXT, 0);

	lv_obj_t* unitLabel = lv_label_create(row);
	lv_label_set_text(unitLabel, unit);
	lv_obj_set_style_text_color(unitLabel, Color::SUBTLE, 0);
	lv_obj_set_style_text_font(unitLabel, &lv_font_montserrat_14, 0);
	lv_obj_set_style_pad_left(unitLabel, 4, 0);

	return card;
}

lv_obj_t* GUI::createProgressCard(lv_obj_t* parent, const char* title,
                                   int32_t min, int32_t max, int32_t init)
{
	lv_obj_t* card = createCard(parent, 300, 80);
	lv_obj_set_layout(card, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
	                       LV_FLEX_ALIGN_CENTER);

	lv_obj_t* titleRow = lv_obj_create(card);
	lv_obj_set_width(titleRow, lv_pct(100));
	lv_obj_set_style_border_width(titleRow, 0, 0);
	lv_obj_set_style_bg_opa(titleRow, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(titleRow, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(titleRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(titleRow, LV_FLEX_ALIGN_SPACE_BETWEEN,
	                       LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	lv_obj_t* titleLabel = lv_label_create(titleRow);
	lv_label_set_text(titleLabel, title);
	lv_obj_set_style_text_color(titleLabel, Color::TEXT, 0);

	// 进度条
	lv_obj_t* bar = createProgressBar(card, lv_pct(100), min, max, init);
	lv_obj_set_height(bar, 16);
	lv_obj_set_style_radius(bar, 8, 0);

	return card;
}

lv_obj_t* GUI::createMenuRow(lv_obj_t* parent, const char* text,
                              lv_obj_t* rightWidget)
{
	lv_obj_t* row = lv_obj_create(parent);
	lv_obj_set_width(row, lv_pct(100));
	lv_obj_set_height(row, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(row, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
	                       LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(row, 8, 0);

	lv_obj_t* label = lv_label_create(row);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_color(label, Color::TEXT, 0);

	if (rightWidget) {
		lv_obj_set_parent(rightWidget, row);
	}

	return row;
}

lv_obj_t* GUI::createImage(lv_obj_t* parent, const void* src)
{
	lv_obj_t* img = lv_image_create(parent);
	lv_image_set_src(img, src);
	return img;
}

lv_obj_t* GUI::createImageFromFile(lv_obj_t* parent, const char* path)
{
	lv_obj_t* img = lv_image_create(parent);
	lv_image_set_src(img, path);
	return img;
}
