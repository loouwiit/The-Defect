#include "gamepadService.hpp"
#include "gamepadManager.hpp"

#include <cstring>
#include <esp_log.h>
#include <host/ble_uuid.h>

static constexpr char TAG[] = "GamepadSvc";

// ── UUID 族 ────────────────────────────────────────────
// UUID: 12b2xxxx-0001-11f0-8b9a-0045cb5d1f2b
// BLE_UUID128_INIT 按传入顺序填入 value[16]，与 UUID 字符串从左到右一致

// Service:   12b20000-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_SERVICE \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x00, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// Input:     12b20001-0001-11f0-8b9a-0045cb5d1f2b (6B 状态包)
#define UUID_INPUT \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x01, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// PlayerID:  12b20002-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_PLAYER_ID \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x02, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

// ── 静态成员定义 ──────────────────────────────────────

const ble_uuid128_t GamepadService::SERVICE_UUID     = UUID_SERVICE;
const ble_uuid128_t GamepadService::CHAR_INPUT_UUID  = UUID_INPUT;
const ble_uuid128_t GamepadService::CHAR_PLAYER_ID_UUID = UUID_PLAYER_ID;

uint16_t GamepadService::s_playerIdValHandle = 0;

// ── 访问回调 ────────────────────────────────────────────

int GamepadService::accessCb(uint16_t connHandle, uint16_t attrHandle,
							 struct ble_gatt_access_ctxt* ctxt, void* arg)
{
	if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		auto len = OS_MBUF_PKTLEN(ctxt->om);
		ESP_LOGI(TAG, "conn=%d WRITE attr=0x%04x len=%d",
				 connHandle, attrHandle, len);

		// Input 包: [buttons(1), joysX(2), joysY(2), battery(1)]
		if (attrHandle == (s_playerIdValHandle - 1) || attrHandle == (s_playerIdValHandle)) {
			// PlayerID char 或它的值句柄 — 手柄请求分配
			auto* slot = GamepadManager::instance().getSlot(connHandle);
			if (slot) {
				ESP_LOGI(TAG, "手柄 conn=%d 请求 Player ID", connHandle);
			}
			return 0;
		}

		// Input characteristic — 解析 6 字节状态包
		if (len >= INPUT_PACKET_SIZE) {
			uint8_t buf[INPUT_PACKET_SIZE];
			ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), nullptr);
			GamepadManager::instance().updateInput(connHandle, buf, len);
			ESP_LOGI(TAG, "conn=%d input: btn=0x%02x joys=(%d,%d) bat=%d%%",
					 connHandle, buf[0],
					 (int16_t)(buf[1] | (uint16_t)(buf[2] << 8)),
					 (int16_t)(buf[3] | (uint16_t)(buf[4] << 8)),
					 buf[5]);
			return 0;
		}

		ESP_LOGW(TAG, "conn=%d 输入包长度不足: %d", connHandle, len);
		return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
	}

	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		ESP_LOGI(TAG, "conn=%d READ attr=0x%04x", connHandle, attrHandle);

		// PlayerID 读取 — 返回分配的 ID
		int8_t playerId = GamepadManager::instance().getPlayerId(connHandle);
		if (playerId < 0) playerId = 0xFF;
		int rc = os_mbuf_append(ctxt->om, &playerId, sizeof(playerId));
		return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
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

int GamepadService::init()
{
	static const struct ble_gatt_svc_def svcDef[] = {
		{
			.type = BLE_GATT_SVC_TYPE_PRIMARY,
			.uuid = &SERVICE_UUID.u,
			.includes = nullptr,
			.characteristics = (const struct ble_gatt_chr_def[]) {
				// ── Input (Write, 6B) ──
				{
					.uuid = &CHAR_INPUT_UUID.u,
					.access_cb = accessCb,
					.arg = nullptr,
					.descriptors = nullptr,
					.flags = BLE_GATT_CHR_F_WRITE,
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

	ESP_LOGI(TAG, "Gamepad Service 已注册 (2 characteristics)");
	return 0;
}
