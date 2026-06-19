#pragma once

#include <cstdint>
#include "host/ble_hs.h"

/**
 * @brief 自定义 Gamepad GATT Service
 *
 * P4 作为 GATT Server，暴露手柄输入 Characteristic。
 * 手柄（Client）通过 Write 方式发送按钮/摇杆数据。
 *
 * Characteristic 一览：
 *   Button    0x12B20001  Write  4B  按钮位掩码
 *   JoystickL 0x12B20002  Write  4B  左摇杆 x(int16) y(int16)
 *   JoystickR 0x12B20003  Write  4B  右摇杆 x(int16) y(int16)
 *   DPad      0x12B20004  Write  1B  方向键位掩码
 *   Battery   0x12B20005  Write  1B  电量 0-100%
 *   PlayerID  0x12B20006  R+W    1B  P4 分配 (0-3)，-1=未分配
 */
class GamepadService
{
public:
	/** 注册 Gamepad Service 到 NimBLE GATT Server */
	static int init();

	/** 更新 Player ID（P4 分配后通知手柄） */
	static int writePlayerId(uint16_t connHandle, int8_t playerId);

	// UUID 族：12b2xxxx-0001-11f0-8b9a-0045cb5d1f2b
	// BLE_UUID128_INIT 按数组顺序填入，与 UUID 字符串从左到右一致
	static const ble_uuid128_t SERVICE_UUID;
	static const ble_uuid128_t CHAR_BUTTON_UUID;
	static const ble_uuid128_t CHAR_JOYSTICK_L_UUID;
	static const ble_uuid128_t CHAR_JOYSTICK_R_UUID;
	static const ble_uuid128_t CHAR_DPAD_UUID;
	static const ble_uuid128_t CHAR_BATTERY_UUID;
	static const ble_uuid128_t CHAR_PLAYER_ID_UUID;

	// Characteristic value handles
	static uint16_t s_playerIdValHandle;

private:
	GamepadService() = delete;

	static constexpr char TAG[] = "GamepadSvc";

	static int accessCb(uint16_t connHandle, uint16_t attrHandle,
						struct ble_gatt_access_ctxt* ctxt, void* arg);
};
