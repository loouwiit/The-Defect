#pragma once

#include <cstdint>
#include "esp_lv_adapter.h"

class App;
class AppStackManager;

/**
 * @brief
 */
class Display
{
public:
	Display();
	~Display();

	bool init();
	bool bindDisplay(esp_lcd_panel_handle_t lcdPanel, esp_lcd_panel_io_handle_t lcdIo, uint16_t horizontalResolution, uint16_t verticalResolution, esp_lv_adapter_tear_avoid_mode_t tearAvoidMode, esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0);
	bool bindTouch(esp_lcd_touch_handle_t touch);
	bool start();

	/** @brief Get LVGL display handle */
	lv_display_t* getLvglDisplay() const
	{
		return lv_disp;
	}

	void setFpsStatisticsEnabled(bool enable = true) const;
	uint32_t getFps() const;
	void applyApp(App* app) const;

	void setStackManager(AppStackManager* manager) { m_stackManager = manager; }
	AppStackManager* getStackManager() const { return m_stackManager; }

	bool lock(int32_t timeout_ms = -1) const
	{
		return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
	}
	void unlock() const
	{
		esp_lv_adapter_unlock();
	}

	class LockGuard
	{
	public:
		explicit LockGuard(const Display& disp, int32_t timeout_ms = -1)
			: m_disp(disp), m_locked(disp.lock(timeout_ms))
		{
		}

		~LockGuard()
		{
			if (m_locked) m_disp.unlock();
		}

		explicit operator bool() const
		{
			return m_locked;
		}

		LockGuard(const LockGuard&) = delete;
		LockGuard& operator=(const LockGuard&) = delete;
		LockGuard(LockGuard&&) = delete;
		LockGuard& operator=(LockGuard&&) = delete;

	private:
		const Display& m_disp;
		bool m_locked;
	};

	LockGuard lockGuard(int32_t timeout_ms = -1) const
	{
		return LockGuard(*this, timeout_ms);
	}

	App* getActiveApp() const { return activeApp; }

private:
	lv_display_t* lv_disp = nullptr;
	mutable App* activeApp{};
	AppStackManager* m_stackManager{};
};
