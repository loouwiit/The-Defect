#include "gamepadManager.hpp"

#include <esp_log.h>

static constexpr char TAG[] = "GamepadMgr";

GamepadManager& GamepadManager::instance()
{
	static GamepadManager inst;
	return inst;
}

int GamepadManager::findFreeSlot()
{
	for (int i = 0; i < MAX_GAMEPADS; i++) {
		if (!slots[i].active) return i;
	}
	return -1;
}

int8_t GamepadManager::assignSlot(uint16_t connHandle)
{
	Lock lock(mutex);

	int idx = findFreeSlot();
	if (idx < 0) {
		ESP_LOGW(TAG, "手柄槽位已满，拒绝连接");
		return -1;
	}

	auto& slot = slots[idx];
	slot.connHandle = connHandle;
	slot.playerId = nextPlayerId;
	slot.active = true;
	slot.state = GamepadState{};

	int8_t assigned = nextPlayerId;
	nextPlayerId = (nextPlayerId + 1) % MAX_GAMEPADS;

	ESP_LOGI(TAG, "手柄 %d 已分配 Player ID=%d (connHandle=%d)",
			 idx, assigned, connHandle);
	return assigned;
}

void GamepadManager::releaseSlot(uint16_t connHandle)
{
	Lock lock(mutex);

	for (int i = 0; i < MAX_GAMEPADS; i++) {
		if (slots[i].active && slots[i].connHandle == connHandle) {
			ESP_LOGI(TAG, "手柄 %d 已释放 (Player ID=%d, connHandle=%d)",
					 i, slots[i].playerId, connHandle);
			slots[i] = GamepadSlot{};
			return;
		}
	}
}

GamepadSlot* GamepadManager::getSlot(uint16_t connHandle)
{
	for (int i = 0; i < MAX_GAMEPADS; i++) {
		if (slots[i].active && slots[i].connHandle == connHandle) {
			return &slots[i];
		}
	}
	return nullptr;
}

GamepadSlot* GamepadManager::getSlotByPlayer(int8_t playerId)
{
	for (int i = 0; i < MAX_GAMEPADS; i++) {
		if (slots[i].active && slots[i].playerId == playerId) {
			return &slots[i];
		}
	}
	return nullptr;
}

void GamepadManager::updateButtons(uint16_t connHandle, uint32_t buttons)
{
	Lock lock(mutex);
	auto* slot = getSlot(connHandle);
	if (slot) slot->state.buttons = buttons;
}

void GamepadManager::updateJoystickL(uint16_t connHandle, int16_t x, int16_t y)
{
	Lock lock(mutex);
	auto* slot = getSlot(connHandle);
	if (slot) {
		slot->state.joysLx = x;
		slot->state.joysLy = y;
	}
}

void GamepadManager::updateJoystickR(uint16_t connHandle, int16_t x, int16_t y)
{
	Lock lock(mutex);
	auto* slot = getSlot(connHandle);
	if (slot) {
		slot->state.joysRx = x;
		slot->state.joysRy = y;
	}
}

void GamepadManager::updateDpad(uint16_t connHandle, uint8_t dpad)
{
	Lock lock(mutex);
	auto* slot = getSlot(connHandle);
	if (slot) slot->state.dpad = dpad;
}

void GamepadManager::updateBattery(uint16_t connHandle, uint8_t level)
{
	Lock lock(mutex);
	auto* slot = getSlot(connHandle);
	if (slot) slot->state.battery = level;
}

int GamepadManager::connectedCount() const
{
	int count = 0;
	for (int i = 0; i < MAX_GAMEPADS; i++) {
		if (slots[i].active) count++;
	}
	return count;
}

int8_t GamepadManager::getPlayerId(uint16_t connHandle) const
{
	for (int i = 0; i < MAX_GAMEPADS; i++) {
		if (slots[i].active && slots[i].connHandle == connHandle) {
			return slots[i].playerId;
		}
	}
	return -1;
}
