#include "gamepadIndev.hpp"
#include "esp_log.h"

GamepadIndev* GamepadIndev::s_instance = nullptr;

// ── 死区常量 ──
static constexpr unsigned char JOYSTICK_DEAD_ZONE = 50;
static constexpr unsigned char JOYSTICK_MIDDLE    = 127;
static constexpr unsigned char JOYSTICK_LEFT      = JOYSTICK_MIDDLE - JOYSTICK_DEAD_ZONE;  // 77
static constexpr unsigned char JOYSTICK_RIGHT     = JOYSTICK_MIDDLE + JOYSTICK_DEAD_ZONE;  // 177

// ════════════════════════════════════════════════════════════════
// 单例
// ════════════════════════════════════════════════════════════════

GamepadIndev& GamepadIndev::instance()
{
	static GamepadIndev inst;
	return inst;
}

// ════════════════════════════════════════════════════════════════
// 启动 / 停止
// ════════════════════════════════════════════════════════════════

bool GamepadIndev::start(Display* display)
{
	if (m_players[0].indev) return true;
	if (!display) return false;

	m_display = display;
	s_instance = this;

	for (int i = 0; i < MaxPlayers; i++)
	{
		auto& slot = m_players[i];

		slot.indev = lv_indev_create();
		if (!slot.indev)
		{
			ESP_LOGE(TAG, "lv_indev_create failed for player %d", i);
			continue;
		}

		lv_indev_set_type(slot.indev, LV_INDEV_TYPE_KEYPAD);
		lv_indev_set_read_cb(slot.indev, indevReadCb);
		lv_indev_set_driver_data(slot.indev, (void*)(intptr_t)i);

		// 与 DesktopApp 原 repeat 参数对齐: 250ms 首次, 100ms 重复
		lv_indev_set_long_press_time(slot.indev, 250);
		lv_indev_set_long_press_repeat_time(slot.indev, 100);

		ESP_LOGI(TAG, "registered KEYPAD indev for player %d", i);
	}

	ESP_LOGI(TAG, "GamepadIndev started (%d players)", MaxPlayers);
	return true;
}

void GamepadIndev::stop()
{
	for (int i = 0; i < MaxPlayers; i++)
	{
		if (m_players[i].indev)
		{
			lv_indev_delete(m_players[i].indev);
			m_players[i].indev = nullptr;
		}
	}
	s_instance = nullptr;
	ESP_LOGI(TAG, "GamepadIndev stopped");
}

lv_indev_t* GamepadIndev::getIndev(uint8_t playerId) const
{
	if (playerId >= MaxPlayers) return nullptr;
	return m_players[playerId].indev;
}

// ════════════════════════════════════════════════════════════════
// 按钮 → LV_KEY 映射表
// ════════════════════════════════════════════════════════════════

struct BtnEntry { uint16_t mask; uint32_t key; };

static constexpr BtnEntry BTN_TABLE[] = {
	{ GamepadButton::BTN_A,      LV_KEY_ENTER },
	{ GamepadButton::BTN_B,      LV_KEY_ESC   },
	{ GamepadButton::BTN_X,      LV_KEY_HOME  },
	{ GamepadButton::BTN_Y,      LV_KEY_END   },
	{ GamepadButton::BTN_SELECT, LV_KEY_PREV  },
	{ GamepadButton::BTN_START,  LV_KEY_NEXT  },
	{ GamepadButton::BTN_L3,     LV_KEY_HOME  },
	{ GamepadButton::BTN_R3,     LV_KEY_END   },
	{ GamepadButton::BTN_TL,     LV_KEY_PREV  },
	{ GamepadButton::BTN_TR,     LV_KEY_NEXT  },
};

// ════════════════════════════════════════════════════════════════
// 从状态构建扁平键列表：按钮 + 摇杆方向 + D-pad
//
// 返回键数。keys 数组最多填 MAX_KEYS 项。
// ════════════════════════════════════════════════════════════════

static uint8_t buildKeyList(const GamepadState& state, uint32_t* keys, uint8_t max)
{
	uint8_t n = 0;

	// 按钮
	for (auto& e : BTN_TABLE)
	{
		if (!state.isPressed(static_cast<GamepadButton>(e.mask))) continue;
		bool dup = false;
		for (uint8_t i = 0; i < n; i++)
			if (keys[i] == e.key) { dup = true; break; }
		if (!dup) keys[n++] = e.key;
		if (n >= max) return n;
	}

	// 摇杆方向
	if (state.lx < JOYSTICK_LEFT)       keys[n++] = LV_KEY_LEFT;
	else if (state.lx > JOYSTICK_RIGHT)  keys[n++] = LV_KEY_RIGHT;
	if (state.ly < JOYSTICK_LEFT)        keys[n++] = LV_KEY_UP;
	else if (state.ly > JOYSTICK_RIGHT)  keys[n++] = LV_KEY_DOWN;

	// D-pad
	if (state.dpad < 8)
	{
		switch (state.dpad)
		{
			case 0: keys[n++] = LV_KEY_UP;    break;
			case 1: keys[n++] = LV_KEY_UP;    break;
			case 2: keys[n++] = LV_KEY_RIGHT; break;
			case 3: keys[n++] = LV_KEY_DOWN;  break;
			case 4: keys[n++] = LV_KEY_DOWN;  break;
			case 5: keys[n++] = LV_KEY_DOWN;  break;
			case 6: keys[n++] = LV_KEY_LEFT;  break;
			case 7: keys[n++] = LV_KEY_UP;    break;
		}
	}

	return n;
}

// ════════════════════════════════════════════════════════════════
// feed — 由 BleGamepad::processTask 调用
//
// 将 GamepadState 转换为按键列表预存在 slot.keys 中，
// indevReadCb 直接遍历即可，无需重复转换。
// ════════════════════════════════════════════════════════════════

void GamepadIndev::feed(uint8_t playerId, const GamepadState& state)
{
	if (playerId >= MaxPlayers) return;
	auto& self = *s_instance;
	auto& slot = self.m_players[playerId];
	if (!slot.indev) return;

	uint32_t keys[PlayerSlot::MAX_KEYS];
	uint8_t count = buildKeyList(state, keys, PlayerSlot::MAX_KEYS);

	if (self.m_mutex.try_lock())
	{
		for (uint8_t i = 0; i < count; i++)
			slot.keys[i] = keys[i];
		slot.keyCount = count;
		slot.seqPos = 0; // 新列表，从头报告
		self.m_mutex.unlock();
	}
}

// ════════════════════════════════════════════════════════════════
// indevReadCb — LVGL worker 任务周期调用
//
// 从 slot.keys 按 seqPos 遍历报告，全部 PRESSED。
// continue_reading 驱动同一周期内连续读取。
// ════════════════════════════════════════════════════════════════

void GamepadIndev::indevReadCb(lv_indev_t* indev, lv_indev_data_t* data)
{
	data->state = LV_INDEV_STATE_REL;
	data->key   = 0;

	if (!s_instance) return;

	auto& self = *s_instance;
	intptr_t playerId = (intptr_t)lv_indev_get_driver_data(indev);
	if (playerId < 0 || playerId >= MaxPlayers) return;

	auto& slot = self.m_players[playerId];

	if (self.m_mutex.try_lock())
	{
		if (slot.seqPos < slot.keyCount)
		{
			data->key   = slot.keys[slot.seqPos];
			data->state = LV_INDEV_STATE_PR;
			slot.seqPos++;

			if (slot.seqPos < slot.keyCount)
				data->continue_reading = true;
			else
				slot.seqPos = 0; // 本轮完毕，等下个轮询
		}
		self.m_mutex.unlock();
	}
}
