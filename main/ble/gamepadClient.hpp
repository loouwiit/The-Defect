#pragma once

#include <cstdint>
#include "host/ble_hs.h"

/**
 * @brief Gamepad GATT Client
 *
 * P4 作为 Central 连接手柄后，发现手柄的 Gamepad Service，
 * 订阅 Input 通知，并在连接时写入手柄 Player ID。
 *
 * 手柄端（Peripheral）Service 定义：
 *   Service  0x12B20000 — Gamepad Service
 *   Input    0x12B20001 — Notify, 6B 状态包
 *   PlayerID 0x12B20002 — Read/Write, 1B
 */
class GamepadClient
{
public:
	static GamepadClient& instance();

	/** 连接建立后触发：开始服务发现 */
	void onConnected(uint16_t connHandle);

	/** 断开后清理 */
	void onDisconnected(uint16_t connHandle);

	// UUID 常量（与手柄端保持一致）
	static const ble_uuid128_t SERVICE_UUID;
	static const ble_uuid128_t CHAR_INPUT_UUID;
	static const ble_uuid128_t CHAR_PLAYER_ID_UUID;

private:
	GamepadClient() = default;
	~GamepadClient() = default;

	static constexpr char TAG[] = "GamepadClient";

	struct Peer {
		uint16_t connHandle = 0;
		bool discovered = false;
		uint16_t inputValHandle = 0;     // Input 值句柄（用于匹配通知）
		uint16_t inputCccdHandle = 0;    // Input CCCD 句柄（用于订阅）
		uint16_t playerIdValHandle = 0;  // PlayerID 值句柄（用于写入）
	};

	static constexpr int MAX_PEERS = 4;
	Peer peers[MAX_PEERS];

	Peer* getPeer(uint16_t connHandle);
	Peer* allocPeer(uint16_t connHandle);

	// NimBLE GATT 发现回调
	static int svcDiscCb(uint16_t connHandle, const struct ble_gatt_error* error,
						 const struct ble_gatt_svc* service, void* arg);
	static int chrDiscCb(uint16_t connHandle, const struct ble_gatt_error* error,
						 const struct ble_gatt_chr* chr, void* arg);
	static int writeCb(uint16_t connHandle, const struct ble_gatt_error* error,
					   struct ble_gatt_attr* attr, void* arg);
};
