#pragma once

#include "esp_lv_adapter_display.h"
class Display;

/**
 * @brief Base application class
 *
 * Represents an "app" that can be applied to the display. Manages an LVGL screen object and provides an interface for initialization and cleanup. Derived classes can implement specific UI and behavior.
 *
 * Init must be called to set the app as running, and deinit must be called to mark it as deletable. The destructor waits until the app is marked deletable before deleting the LVGL screen object.
 * In the initialization phase, the app can doing time consuming tasks, and in the cleanup phase, it can stop those tasks.
 *
 * Please ensure the constructor is simple for fast construction. If there are complex steps, please move them to init.
 */
class App
{
public:
	App(Display* display);
	virtual ~App();

	virtual void init() { running = true; deletable = false; };
	virtual void deinit() { running = false; deletable = true; };

protected:
	Display* display{};
	lv_obj_t* screen{};

	bool running{};
	bool deletable{ true };

	friend class Display;
};

#include "display/display.hpp"
