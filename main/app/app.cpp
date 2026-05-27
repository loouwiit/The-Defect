#include "app.hpp"

App::App(Display* display) :
	display{ display },
	screen{ lv_obj_create(nullptr) }
{
	lv_obj_set_size(screen, lv_display_get_vertical_resolution(display->getLvglDisplay()), lv_display_get_horizontal_resolution(display->getLvglDisplay()));
	lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_scroll_dir(screen, LV_DIR_NONE);
}

App::~App()
{
	if (running)
		deinit();

	while (deletable)
		vTaskDelay(10);

	lv_obj_delete(screen);
}
