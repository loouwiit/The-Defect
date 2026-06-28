#pragma once

#include <cstdint>
#include "bleGamepad/gamepadState.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

class GamepadIndev
{
public:
	static GamepadIndev& instance();

	bool start();
	void stop();

	void feed(uint8_t playerId, const GamepadState& state);

	void bindGroup(uint8_t playerId, lv_group_t* group);

private:
	GamepadIndev() = default;
	~GamepadIndev() = default;
	GamepadIndev(const GamepadIndev&) = delete;
	GamepadIndev& operator=(const GamepadIndev&) = delete;

	static constexpr unsigned char JoystickDeadZone = 70;
	static constexpr unsigned char JoystickMiddle = 127;
	static constexpr unsigned char JoystickLeft = JoystickMiddle - JoystickDeadZone;
	static constexpr unsigned char JoystickRight = JoystickMiddle + JoystickDeadZone;

	enum class KeyTable
	{
		Enter = 0,
		ESC,
		Backspace,
		Delete,
		Home,
		End,
		Previous,
		Next,
		Up,
		Down,
		Left,
		Right,
		count,
	};

	constexpr static uint32_t KeyMap[(size_t)KeyTable::count]
	{
		lv_key_t::LV_KEY_ENTER,
		lv_key_t::LV_KEY_ESC,
		lv_key_t::LV_KEY_BACKSPACE,
		lv_key_t::LV_KEY_DEL,
		lv_key_t::LV_KEY_HOME,
		lv_key_t::LV_KEY_END,
		lv_key_t::LV_KEY_PREV,
		lv_key_t::LV_KEY_NEXT,
		lv_key_t::LV_KEY_UP,
		lv_key_t::LV_KEY_DOWN,
		lv_key_t::LV_KEY_LEFT,
		lv_key_t::LV_KEY_RIGHT,
	};

	constexpr static struct { GamepadButton button{}; KeyTable key{}; } ButtonMap[]
	{
		{GamepadButton::BTN_A, KeyTable::Enter},
		{GamepadButton::BTN_B, KeyTable::ESC},
		{GamepadButton::BTN_X, KeyTable::Backspace},
		{GamepadButton::BTN_Y, KeyTable::Delete},
		{GamepadButton::BTN_SELECT, KeyTable::End},
		{GamepadButton::BTN_START, KeyTable::Home},
		{GamepadButton::BTN_L3, KeyTable::Enter},
		{GamepadButton::BTN_R3, KeyTable::Enter},
		{GamepadButton::BTN_TL, KeyTable::Previous},
		{GamepadButton::BTN_TR, KeyTable::Next},
	};

	struct KeySM
	{
		bool prevPressed = false;
		TickType_t nextRepeat = 0;
		bool repeating = false;
	};

	struct PlayerSlot
	{
		bool keys[(size_t)KeyTable::count]{};
		KeySM sm[(size_t)KeyTable::count];
		lv_group_t* group = nullptr;
	};

	PlayerSlot m_players[MaxPlayers];

	static GamepadIndev* s_instance;

	static void processKeys();
	static void sendKey(lv_group_t* group, KeyTable key);

	static constexpr char TAG[] = "GamepadIndev";
};
