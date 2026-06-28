#pragma once

#include <cstdint>
#include "bleGamepad/gamepadState.hpp"
#include "mutex/mutex.hpp"
#include "display/display.hpp"
#include "lvgl.h"

class GamepadIndev
{
public:
	static GamepadIndev& instance();

	bool start(Display* display);
	void stop();

	/** 由 BleGamepad 调用，将手柄状态注入到对应玩家的 KEYPAD indev */
	void feed(uint8_t playerId, const GamepadState& state);

	/** 获取指定玩家的 LVGL indev 句柄，用于 lv_indev_set_group() 等操作 */
	lv_indev_t* getIndev(uint8_t playerId) const;

private:
	GamepadIndev() = default;
	~GamepadIndev() = default;
	GamepadIndev(const GamepadIndev&) = delete;
	GamepadIndev& operator=(const GamepadIndev&) = delete;

	struct PlayerSlot {
		lv_indev_t* indev = nullptr;

		// — 预构建的按键列表（由 feed 写入 / indevReadCb 读取，mutex 保护）—
		static constexpr uint8_t MAX_KEYS = 12;
		uint32_t keys[MAX_KEYS];
		uint8_t keyCount = 0;

		// — 报告位置 —
		uint8_t seqPos = 0;
	};

	PlayerSlot m_players[MaxPlayers];
	Display* m_display = nullptr;
	Mutex m_mutex;

	static GamepadIndev* s_instance;

	static void indevReadCb(lv_indev_t* indev, lv_indev_data_t* data);

	static constexpr char TAG[] = "GamepadIndev";
};
