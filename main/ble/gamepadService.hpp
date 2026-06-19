#pragma once

#include <cstdint>
#include "host/ble_hs.h"

/**
 * @brief 自定义 Gamepad GATT Service
 *
 * P4 作为 GATT Server，暴露手柄输入 Characteristic。
 * 手柄（Client）通过 Write 发送 6 字节状态包。
 *
 * Characteristic 一览：
 *   Input    0x12B20001  Write  6B  手柄状态包
 *               [0] buttons: bit0=A,bit1=B,bit2=X,bit3=Y,bit4=Start,bit5=Select
 *               [1-2] joysX: int16 小端
 *               [3-4] joysY: int16 小端
 *               [5] battery: 0-100
 *   PlayerID 0x12B20002  R+W    1B  P4 分配 (0-3)
 */
class GamepadService
{
public:
	static int init();
	static int writePlayerId(uint16_t connHandle, int8_t playerId);

	static const ble_uuid128_t SERVICE_UUID;
	static const ble_uuid128_t CHAR_INPUT_UUID;
	static const ble_uuid128_t CHAR_PLAYER_ID_UUID;

	static uint16_t s_playerIdValHandle;

private:
	GamepadService() = delete;
	static constexpr char TAG[] = "GamepadSvc";
	static int accessCb(uint16_t connHandle, uint16_t attrHandle,
						struct ble_gatt_access_ctxt* ctxt, void* arg);
};
