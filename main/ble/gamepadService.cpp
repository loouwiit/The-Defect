#include "gamepadService.hpp"
#include "gamepadManager.hpp"

#include <cstring>
#include <esp_log.h>
#include <host/ble_uuid.h>

static constexpr char TAG[] = "GamepadSvc";

// ── UUID 族 ────────────────────────────────────────────
// UUID: 12b2xxxx-0001-11f0-8b9a-0045cb5d1f2b
// BLE_UUID128_INIT 按传入顺序填入 value[16]，与 UUID 字符串从左到右一致

// Service:    12b20000-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_SERVICE \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x00, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// Button:     12b20001-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_BUTTON \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x01, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// JoystickL:  12b20002-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_JOYSTICK_L \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x02, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// JoystickR:  12b20003-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_JOYSTICK_R \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x03, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// D-Pad:      12b20004-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_DPAD \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x04, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// Battery:    12b20005-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_BATTERY \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x05, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// PlayerID:   12b20006-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_PLAYER_ID \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x06, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// ── 静态成员定义 ──────────────────────────────────────

const ble_uuid128_t GamepadService::SERVICE_UUID      = UUID_SERVICE;
const ble_uuid128_t GamepadService::CHAR_BUTTON_UUID   = UUID_BUTTON;
const ble_uuid128_t GamepadService::CHAR_JOYSTICK_L_UUID = UUID_JOYSTICK_L;
const ble_uuid128_t GamepadService::CHAR_JOYSTICK_R_UUID = UUID_JOYSTICK_R;
const ble_uuid128_t GamepadService::CHAR_DPAD_UUID     = UUID_DPAD;
const ble_uuid128_t GamepadService::CHAR_BATTERY_UUID  = UUID_BATTERY;
const ble_uuid128_t GamepadService::CHAR_PLAYER_ID_UUID = UUID_PLAYER_ID;

uint16_t GamepadService::s_playerIdValHandle = 0;

// ── 访问回调 ────────────────────────────────────────────

int GamepadService::accessCb(uint16_t connHandle, uint16_t attrHandle,
							 struct ble_gatt_access_ctxt* ctxt, void* arg)
{
	// ctxt->chr->uuid 的类型是 const ble_uuid_t*
	// 但 clang 需要二次解引用，通过局部变量保持代码简洁
	const ble_uuid_t* uuid = (ctxt->chr != nullptr)
		? (const ble_uuid_t*)ctxt->chr->uuid
		: nullptr;

	if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		ESP_LOGI(TAG, "conn=%d WRITE attr=0x%04x len=%d",
				 connHandle, attrHandle, OS_MBUF_PKTLEN(ctxt->om));

		// ── 手柄写入数据 → 更新 GamepadManager ──
		uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

		if (ble_uuid_cmp(uuid, &CHAR_BUTTON_UUID.u) == 0 && len >= 4) {
			uint32_t val;
			ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), nullptr);
			GamepadManager::instance().updateButtons(connHandle, val);
			ESP_LOGI(TAG, "conn=%d buttons=0x%08" PRIx32, connHandle, val);
			return 0;
		}
		if (ble_uuid_cmp(uuid, &CHAR_JOYSTICK_L_UUID.u) == 0 && len >= 4) {
			struct { int16_t x; int16_t y; } val;
			ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), nullptr);
			GamepadManager::instance().updateJoystickL(connHandle, val.x, val.y);
			ESP_LOGI(TAG, "conn=%d joysL=(%d,%d)", connHandle, val.x, val.y);
			return 0;
		}
		if (ble_uuid_cmp(uuid, &CHAR_JOYSTICK_R_UUID.u) == 0 && len >= 4) {
			struct { int16_t x; int16_t y; } val;
			ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), nullptr);
			GamepadManager::instance().updateJoystickR(connHandle, val.x, val.y);
			ESP_LOGI(TAG, "conn=%d joysR=(%d,%d)", connHandle, val.x, val.y);
			return 0;
		}
		if (ble_uuid_cmp(uuid, &CHAR_DPAD_UUID.u) == 0 && len >= 1) {
			uint8_t val;
			ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), nullptr);
			GamepadManager::instance().updateDpad(connHandle, val);
			ESP_LOGI(TAG, "conn=%d dpad=0x%02x", connHandle, val);
			return 0;
		}
		if (ble_uuid_cmp(uuid, &CHAR_BATTERY_UUID.u) == 0 && len >= 1) {
			uint8_t val;
			ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), nullptr);
			GamepadManager::instance().updateBattery(connHandle, val);
			ESP_LOGI(TAG, "conn=%d battery=%d%%", connHandle, val);
			return 0;
		}
		if (ble_uuid_cmp(uuid, &CHAR_PLAYER_ID_UUID.u) == 0) {
			// 手柄请求分配 Player ID (写任意值触发)
			auto* slot = GamepadManager::instance().getSlot(connHandle);
			if (slot) {
				// 将分配的 Player ID 写回 Characteristic Value
				// (NimBLE 会自动通知 handle，无需额外操作)
				ESP_LOGI(TAG, "手柄 conn=%d 请求 Player ID", connHandle);
			}
			return 0;
		}
	}

	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		ESP_LOGI(TAG, "conn=%d READ attr=0x%04x", connHandle, attrHandle);

		// ── 手柄读取数据 ──
		if (ble_uuid_cmp(uuid, &CHAR_PLAYER_ID_UUID.u) == 0) {
			int8_t playerId = GamepadManager::instance().getPlayerId(connHandle);
			if (playerId < 0) playerId = 0xFF; // 未分配
			int rc = os_mbuf_append(ctxt->om, &playerId, sizeof(playerId));
			return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
		}
		if (ble_uuid_cmp(uuid, &CHAR_BATTERY_UUID.u) == 0) {
			auto* slot = GamepadManager::instance().getSlot(connHandle);
			uint8_t level = slot ? slot->state.battery : 0;
			int rc = os_mbuf_append(ctxt->om, &level, sizeof(level));
			return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
		}
	}

	ESP_LOGW(TAG, "conn=%d 未匹配的访问 op=%d attr=0x%04x",
			 connHandle, ctxt->op, attrHandle);
	return BLE_ATT_ERR_UNLIKELY;
}

// ── 写 Player ID（P4 分配后写入） ─────────────────────

int GamepadService::writePlayerId(uint16_t connHandle, int8_t playerId)
{
	if (s_playerIdValHandle == 0) return BLE_HS_ENOTCONN;
	ble_gatts_chr_updated(s_playerIdValHandle);
	return 0;
}

// ── GATT Service 定义表 ────────────────────────────────
// 注：C++ 中 designated initializer 必须按 struct 字段声明顺序

int GamepadService::init()
{
	// ble_gatt_svc_def: type, uuid, includes, characteristics
	static const struct ble_gatt_svc_def svcDef[] = {
		{
			.type = BLE_GATT_SVC_TYPE_PRIMARY,
			.uuid = &SERVICE_UUID.u,
			.includes = nullptr,
			.characteristics = (const struct ble_gatt_chr_def[]) {
				// ── Button (Write) ──
				// ble_gatt_chr_def: uuid, access_cb, arg, descriptors, flags, min_key_size, val_handle, cpfd
				{
					.uuid = &CHAR_BUTTON_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_WRITE,
					.min_key_size = 0,
					.val_handle = nullptr,
					.cpfd = nullptr,
				},
				// ── Joystick L (Write) ──
				{
					.uuid = &CHAR_JOYSTICK_L_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_WRITE,
					.min_key_size = 0,
					.val_handle = nullptr,
					.cpfd = nullptr,
				},
				// ── Joystick R (Write) ──
				{
					.uuid = &CHAR_JOYSTICK_R_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_WRITE,
					.min_key_size = 0,
					.val_handle = nullptr,
					.cpfd = nullptr,
				},
				// ── D-Pad (Write) ──
				{
					.uuid = &CHAR_DPAD_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_WRITE,
					.min_key_size = 0,
					.val_handle = nullptr,
					.cpfd = nullptr,
				},
				// ── Battery (Read + Write) ──
				{
					.uuid = &CHAR_BATTERY_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
					.min_key_size = 0,
					.val_handle = nullptr,
					.cpfd = nullptr,
				},
				// ── Player ID (Read + Write) ──
				{
					.uuid = &CHAR_PLAYER_ID_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
					.min_key_size = 0,
					.val_handle = &s_playerIdValHandle,
					.cpfd = nullptr,
				},
				// ── 结束 ──
				{
					.uuid = nullptr,
					.access_cb = nullptr,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = 0,
					.min_key_size = 0,
					.val_handle = nullptr,
					.cpfd = nullptr,
				},
			},
		},
		// ── 服务表结束 ──
		{
			.type = 0,
			.uuid = nullptr,
			.includes = nullptr,
			.characteristics = nullptr,
		},
	};

	int rc = ble_gatts_count_cfg(svcDef);
	if (rc != 0) {
		ESP_LOGE(TAG, "GATT 服务计数失败: %d", rc);
		return rc;
	}

	rc = ble_gatts_add_svcs(svcDef);
	if (rc != 0) {
		ESP_LOGE(TAG, "GATT 服务注册失败: %d", rc);
		return rc;
	}

	ESP_LOGI(TAG, "Gamepad Service 已注册 (6 characteristics)");
	return 0;
}
