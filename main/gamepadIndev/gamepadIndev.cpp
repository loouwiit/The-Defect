#include "gamepadIndev.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/display.hpp"

GamepadIndev* GamepadIndev::s_instance = nullptr;

static constexpr TickType_t REPEAT_DELAY_MS = 250;
static constexpr TickType_t REPEAT_RATE_MS = 100;

GamepadIndev& GamepadIndev::instance()
{
	static GamepadIndev inst{};
	return inst;
}

bool GamepadIndev::start()
{
	if (s_instance) return true;
	s_instance = this;
	ESP_LOGI(TAG, "GamepadIndev started (%d players)", MaxPlayers);
	return true;
}

void GamepadIndev::stop()
{
	for (auto& slot : m_players)
	{
		slot.group = nullptr;
		for (auto& k : slot.keys) k = false;
		for (auto& s : slot.sm) s = {};
	}
	s_instance = nullptr;
	ESP_LOGI(TAG, "GamepadIndev stopped");
}

void GamepadIndev::bindGroup(uint8_t playerId, lv_group_t* group)
{
	if (playerId >= MaxPlayers) return;
	m_players[playerId].group = group;
}

// ════════════════════════════════════════════════════════════════
// processKeys — 状态机处理（BLE 任务上下文）
//
// 遍历所有玩家的按键状态，检测边沿 / repeat，发送到对应 group。
// feed 调用此函数时尝试获取 LVGL 锁。
// ════════════════════════════════════════════════════════════════

void GamepadIndev::processKeys()
{
	auto& self = *s_instance;

	if (esp_lv_adapter_lock(10) != ESP_OK) return;

	TickType_t now = xTaskGetTickCount();

	for (auto& slot : self.m_players)
	{
		if (!slot.group) continue;

		for (int i = 0; i < (int)KeyTable::count; i++)
		{
			bool pressed = slot.keys[i];
			auto& sm = slot.sm[i];

			if (pressed && !sm.prevPressed)
			{
				sendKey(slot.group, (KeyTable)i);
				sm.nextRepeat = now + pdMS_TO_TICKS(REPEAT_DELAY_MS);
				sm.repeating = false;
			}
			else if (pressed && sm.prevPressed)
			{
				if (now >= sm.nextRepeat)
				{
					sendKey(slot.group, (KeyTable)i);
					TickType_t interval = sm.repeating
						? pdMS_TO_TICKS(REPEAT_RATE_MS)
						: pdMS_TO_TICKS(REPEAT_DELAY_MS);
					sm.nextRepeat = now + interval;
					sm.repeating = true;
				}
			}

			sm.prevPressed = pressed;
		}
	}

	esp_lv_adapter_unlock();
}

// ════════════════════════════════════════════════════════════════
// feed — 由 BleGamepad::processTask 调用（~60Hz）
//
// 1. 更新按键数组
// 2. 立刻处理状态机（尝试拿锁 10ms，拿不到就跳过本次）
// ════════════════════════════════════════════════════════════════

void GamepadIndev::feed(uint8_t playerId, const GamepadState& state)
{
	auto& self = *s_instance;
	auto& slot = self.m_players[playerId];
	bool(&keys)[(size_t)KeyTable::count] = slot.keys;

	for (auto& i : keys) i = false;
	for (auto& i : ButtonMap)
		keys[(int)i.key] |= state.isPressed(i.button);

	keys[(int)KeyTable::Up]    |= state.ly < JoystickLeft;
	keys[(int)KeyTable::Down]  |= state.ly > JoystickRight;
	keys[(int)KeyTable::Left]  |= state.lx < JoystickLeft;
	keys[(int)KeyTable::Right] |= state.lx > JoystickRight;

	keys[(int)KeyTable::Up]    |= state.dpad == 0 || state.dpad == 1 || state.dpad == 7;
	keys[(int)KeyTable::Down]  |= state.dpad == 3 || state.dpad == 4 || state.dpad == 5;
	keys[(int)KeyTable::Left]  |= state.dpad == 5 || state.dpad == 6 || state.dpad == 7;
	keys[(int)KeyTable::Right] |= state.dpad == 1 || state.dpad == 2 || state.dpad == 3;

	// 每一次 feed 后立即处理状态机
	processKeys();
}

// ════════════════════════════════════════════════════════════════
// 发送按键到 group
// ════════════════════════════════════════════════════════════════

void GamepadIndev::sendKey(lv_group_t* group, KeyTable key)
{
	lv_group_send_data(group, KeyMap[(int)key]);
}
