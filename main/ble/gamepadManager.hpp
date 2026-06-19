#pragma once

#include <cstdint>
#include "mutex/mutex.hpp"

/**
 * @brief 手柄数据快照（从 GATT Write 回调更新）
 */
struct GamepadState
{
	uint32_t buttons = 0;       // 按钮位掩码
	int16_t joysLx = 0;         // 左摇杆 X
	int16_t joysLy = 0;         // 左摇杆 Y
	int16_t joysRx = 0;         // 右摇杆 X
	int16_t joysRy = 0;         // 右摇杆 Y
	uint8_t dpad = 0;           // 方向键位掩码
	uint8_t battery = 0;        // 电量 0-100
};

/**
 * @brief 手柄槽位（每个连接一个）
 */
struct GamepadSlot
{
	uint16_t connHandle = 0;
	int8_t playerId = -1;       // -1 = 未分配
	bool active = false;
	GamepadState state;
};

/**
 * @brief 多手柄管理器
 *
 * 单例。管理最多 4 个手柄的连接/断开/数据缓存。
 * 由 GamepadService 的 access callback 更新数据。
 * 由 GamepadInput 定时轮询读取。
 */
class GamepadManager
{
public:
	static GamepadManager& instance();

	/** 分配 Player ID 并激活槽位，返回 playerId (-1=已满) */
	int8_t assignSlot(uint16_t connHandle);

	/** 释放槽位 */
	void releaseSlot(uint16_t connHandle);

	/** 根据 connHandle 获取槽位指针，nullptr=无效 */
	GamepadSlot* getSlot(uint16_t connHandle);

	/** 根据 Player ID 获取槽位指针 */
	GamepadSlot* getSlotByPlayer(int8_t playerId);

	/** 更新手柄数据 */
	void updateButtons(uint16_t connHandle, uint32_t buttons);
	void updateJoystickL(uint16_t connHandle, int16_t x, int16_t y);
	void updateJoystickR(uint16_t connHandle, int16_t x, int16_t y);
	void updateDpad(uint16_t connHandle, uint8_t dpad);
	void updateBattery(uint16_t connHandle, uint8_t level);

	/** 查询 */
	int connectedCount() const;
	int8_t getPlayerId(uint16_t connHandle) const;

	static constexpr int MAX_GAMEPADS = 4;

private:
	GamepadManager() = default;
	~GamepadManager() = default;

	GamepadSlot slots[MAX_GAMEPADS];
	Mutex mutex;
	int8_t nextPlayerId = 0;

	int findFreeSlot();
};
