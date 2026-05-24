#include "app.hpp"

App::App(Display* display) :
	display{ display },
	screen{ lv_obj_create(nullptr) }
{
}

App::~App()
{
	if (running)
		deinit();

	while (deletable)
		vTaskDelay(10);

	lv_obj_delete(screen);
}
