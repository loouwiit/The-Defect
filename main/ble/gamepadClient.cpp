#include "gamepadClient.hpp"
#include "gamepadManager.hpp"

#include <cstring>
#include <esp_log.h>
#include <host/ble_uuid.h>

static constexpr char TAG[] = "GamepadClient";

// ── UUID 族 ────────────────────────────────────────────
// 与手柄端 Peripheral 保持一致

// Service:   12b20000-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_SERVICE \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x00, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)
// Input:     12b20001-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_INPUT \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x01, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)
// PlayerID:  12b20002-0001-11f0-8b9a-0045cb5d1f2b
#define UUID_PLAYER_ID \
    BLE_UUID128_INIT(0x12,0xb2,0x00,0x02, 0x00,0x01, 0x11,0xf0, 0x8b,0x9a, 0x00,0x45,0xcb,0x5d,0x1f,0x2b)

const ble_uuid128_t GamepadClient::SERVICE_UUID       = UUID_SERVICE;
const ble_uuid128_t GamepadClient::CHAR_INPUT_UUID    = UUID_INPUT;
const ble_uuid128_t GamepadClient::CHAR_PLAYER_ID_UUID = UUID_PLAYER_ID;

// ── 单例 ────────────────────────────────────────────────

GamepadClient& GamepadClient::instance()
{
	static GamepadClient inst;
	return inst;
}

// ── Peer 管理 ──────────────────────────────────────────

GamepadClient::Peer* GamepadClient::getPeer(uint16_t connHandle)
{
	for (auto& p : peers) {
		if (p.connHandle == connHandle) return &p;
	}
	return nullptr;
}

GamepadClient::Peer* GamepadClient::allocPeer(uint16_t connHandle)
{
	for (auto& p : peers) {
		if (p.connHandle == 0 || p.connHandle == connHandle) {
			p = Peer{}; // reset
			p.connHandle = connHandle;
			return &p;
		}
	}
	return nullptr;
}

// ── 写回调（通用） ────────────────────────────────────

int GamepadClient::writeCb(uint16_t connHandle,
						   const struct ble_gatt_error* error,
						   struct ble_gatt_attr* attr, void* arg)
{
	if (error->status != 0) {
		ESP_LOGW(TAG, "conn=%d GATT 写入失败 attr=0x%04x status=%d",
				 connHandle, attr ? attr->handle : 0, error->status);
	}
	return 0;
}

// ── 特征发现回调 ──────────────────────────────────────

int GamepadClient::chrDiscCb(uint16_t connHandle,
							 const struct ble_gatt_error* error,
							 const struct ble_gatt_chr* chr, void* arg)
{
	if (error->status == BLE_HS_EDONE) {
		// 所有特征发现完成
		auto* peer = instance().getPeer(connHandle);
		if (!peer) return 0;

		if (!peer->inputCccdHandle) {
			ESP_LOGW(TAG, "conn=%d 未找到 Input characteristic", connHandle);
			return 0;
		}

		// 订阅 Input 通知：写 0x0001 到 CCCD
		uint8_t val[] = { 0x01, 0x00 };
		int rc = ble_gattc_write_flat(connHandle, peer->inputCccdHandle,
									  val, sizeof(val), writeCb, nullptr);
		if (rc != 0) {
			ESP_LOGE(TAG, "conn=%d CCCD 写入失败: %d", connHandle, rc);
		} else {
			ESP_LOGI(TAG, "conn=%d 已订阅 Input 通知", connHandle);

			// 写 Player ID 到手柄
			int8_t pid = GamepadManager::instance().getPlayerId(connHandle);
			if (pid >= 0 && peer->playerIdValHandle) {
				ble_gattc_write_flat(connHandle, peer->playerIdValHandle,
									 &pid, sizeof(pid), writeCb, nullptr);
				ESP_LOGI(TAG, "conn=%d Player ID=%d 已写入", connHandle, pid);
			}
		}
		return 0;
	}

	if (error->status != 0) return 0;
	if (!chr) return 0;

	auto* peer = instance().getPeer(connHandle);
	if (!peer) return 0;

	// 匹配特征 UUID
	if (ble_uuid_cmp(&chr->uuid.u, &CHAR_INPUT_UUID.u) == 0) {
		peer->inputValHandle = chr->val_handle;
		peer->inputCccdHandle = chr->val_handle + 1; // CCCD = val_handle + 1
		ESP_LOGI(TAG, "conn=%d Input char: val=0x%04x cccd=0x%04x",
				 connHandle, chr->val_handle, chr->val_handle + 1);
	}
	if (ble_uuid_cmp(&chr->uuid.u, &CHAR_PLAYER_ID_UUID.u) == 0) {
		peer->playerIdValHandle = chr->val_handle;
		ESP_LOGI(TAG, "conn=%d PlayerID char: val=0x%04x",
				 connHandle, chr->val_handle);
	}

	return 0;
}

// ── 服务发现回调 ──────────────────────────────────────

int GamepadClient::svcDiscCb(uint16_t connHandle,
							 const struct ble_gatt_error* error,
							 const struct ble_gatt_svc* service, void* arg)
{
	if (error->status == BLE_HS_EDONE) {
		// 所有服务发现完成
		return 0;
	}

	if (error->status != 0 || !service) return 0;

	// 检查是否是我们的 Gamepad Service
	if (ble_uuid_cmp(&service->uuid.u, &SERVICE_UUID.u) == 0) {
		ESP_LOGI(TAG, "conn=%d 发现 Gamepad Service (handle=%d-%d)",
				 connHandle, service->start_handle, service->end_handle);
		// 发现该服务的所有特征
		int rc = ble_gattc_disc_all_chrs(connHandle, service->start_handle,
										 service->end_handle,
										 chrDiscCb, nullptr);
		if (rc != 0) {
			ESP_LOGE(TAG, "conn=%d 特征发现失败: %d", connHandle, rc);
		}
	}
	return 0;
}

// ── 公开接口 ───────────────────────────────────────────

void GamepadClient::onConnected(uint16_t connHandle)
{
	auto* peer = allocPeer(connHandle);
	if (!peer) {
		ESP_LOGE(TAG, "conn=%d peer 槽位已满", connHandle);
		return;
	}

	// 分配 Player ID
	int8_t pid = GamepadManager::instance().assignSlot(connHandle);
	if (pid < 0) {
		ESP_LOGW(TAG, "conn=%d 手柄槽位已满，断开", connHandle);
		ble_gap_terminate(connHandle, BLE_ERR_CONN_TERM_LOCAL);
		return;
	}
	ESP_LOGI(TAG, "conn=%d Player ID=%d 已分配", connHandle, pid);

	// 开始服务发现
	int rc = ble_gattc_disc_all_svcs(connHandle, svcDiscCb, nullptr);
	if (rc != 0) {
		ESP_LOGE(TAG, "conn=%d 服务发现失败: %d", connHandle, rc);
	}
}

void GamepadClient::onDisconnected(uint16_t connHandle)
{
	auto* peer = getPeer(connHandle);
	if (peer) {
		*peer = Peer{}; // 清理
	}
}
