#pragma once

#include "esp_lv_adapter_display.h"
#include "bleGamepad/gamepadState.hpp"
#include <esp_log.h>
class Display;
class AppStackManager;

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

	// BLE 手柄输入 — 由 BleGamepad 路由到当前活跃 App
	virtual void onGamepadInput(uint8_t playerId, const GamepadState& state) {}
	virtual void onGamepadConnected(uint8_t playerId) { ESP_LOGI(TAG, "Player %d connected", playerId); }
	virtual void onGamepadDisconnected(uint8_t playerId) { ESP_LOGI(TAG, "Player %d disconnected", playerId); }

	// ── 栈生命周期钩子（由 AppStackManager 调用） ──
	/** @brief App 成为前台活跃（已 visible） */
	virtual void onForeground() {}
	/** @brief App 被覆盖/移出前台（即将 invisible） */
	virtual void onBackground() {}

	// ── 便利方法（委托给 AppStackManager） ──
	void pushApp(App* app);              // push 到当前栈
	void popApp();                       // 弹出当前栈顶
	void replaceWith(App* app);          // 替换当前栈顶

	/** @brief 设置所属管理器（由 AppStackManager/AppStack 自动调用） */
	void setManager(AppStackManager* manager);
	AppStackManager* getManager() const { return m_manager; }
	Display* getDisplay() const { return display; }

protected:
	Display* display{};
	lv_obj_t* screen{};

	bool running{};
	bool deletable{ true };

	AppStackManager* m_manager{};

	friend class Display;
	friend class AppStack;
	friend class AppStackManager;

private:
	constexpr static char TAG[] = "App";
};

#include "display/display.hpp"
