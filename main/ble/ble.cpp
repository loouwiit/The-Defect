#include "ble.hpp"
#include "gamepadClient.hpp"
#include "gamepadManager.hpp"

#include <cstring>
#include <esp_log.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <esp_hosted.h>

// ble_store_config_init 定义在 NimBLE 源码的 store/config 中
extern "C" void ble_store_config_init(void);

static constexpr char TAG[] = "Ble";

Ble* Ble::s_instance = nullptr;

// 手柄 Service UUID 的 bytes（用于扫描过滤）
static const ble_uuid128_t GAMEPAD_SVC_UUID = GamepadClient::SERVICE_UUID;

// ── 单例 ────────────────────────────────────────────────

Ble& Ble::instance()
{
	static Ble inst;
	return inst;
}

// ── 扫描回调 ──────────────────────────────────────────

/**
 * 检查广播数据中是否包含指定的 128-bit UUID
 */
static bool advDataHasUuid(const uint8_t* data, uint8_t len,
						   const ble_uuid128_t* uuid)
{
	// 简单检查：遍历 AD 结构，查找 UUID 列表
	uint8_t pos = 0;
	while (pos + 1 < len) {
		uint8_t fieldLen = data[pos];
		if (fieldLen == 0) break;
		uint8_t fieldType = data[pos + 1];
		// 检查完整 128-bit UUID（类型 0x07=Complete, 0x06=Incomplete）
		if ((fieldType == 0x07 || fieldType == 0x06) && fieldLen >= 17) {
			if (memcmp(data + pos + 2, uuid->value, 16) == 0)
				return true;
		}
		pos += fieldLen + 1;
	}
	return false;
}

static int discCb(struct ble_gap_event* event, void* arg)
{
	if (event->type == BLE_GAP_EVENT_DISC) {
		const auto& disc = event->disc;

		// 地址转字符串
		char addrStr[18];
		sprintf(addrStr, "%02x:%02x:%02x:%02x:%02x:%02x",
				disc.addr.val[5], disc.addr.val[4], disc.addr.val[3],
				disc.addr.val[2], disc.addr.val[1], disc.addr.val[0]);

		// 尝试提取设备名（AD 数据中找 type=0x08/0x09）
		char nameBuf[32] = "(unnamed)";
		if (disc.length_data > 0) {
			uint8_t pos = 0;
			while (pos + 1 < disc.length_data) {
				uint8_t flen = disc.data[pos];
				if (flen == 0) break;
				uint8_t ftype = disc.data[pos + 1];
				if ((ftype == 0x08 || ftype == 0x09) && flen > 1) {
					uint8_t nlen = flen - 1;
					if (nlen > sizeof(nameBuf) - 1) nlen = sizeof(nameBuf) - 1;
					memcpy(nameBuf, disc.data + pos + 2, nlen);
					nameBuf[nlen] = '\0';
					break;
				}
				pos += flen + 1;
			}
		}

		ESP_LOGI(TAG, "扫描到: %s (%s) rssi=%d", addrStr, nameBuf, disc.rssi);

		// 检查广播数据中是否包含我们的 Service UUID
		bool hasSvc = false;
		if (disc.length_data > 0)
			hasSvc = advDataHasUuid(disc.data, disc.length_data, &GAMEPAD_SVC_UUID);

		if (!hasSvc) return 0;

		// 找到手柄，停止扫描并连接
		ESP_LOGI(TAG, "→ 发现手柄 Service, 正在连接...");

		int rc = ble_gap_disc_cancel();
		if (rc != 0 && rc != BLE_HS_EALREADY) {
			ESP_LOGW(TAG, "停止扫描失败: %d", rc);
		}

		// 连接手柄
		struct ble_gap_conn_params connParams;
		std::memset(&connParams, 0, sizeof(connParams));
		connParams.scan_itvl = 0x40;
		connParams.scan_window = 0x40;
		connParams.itvl_min = 24;   // 30ms
		connParams.itvl_max = 40;   // 50ms
		connParams.latency = 0;
		connParams.supervision_timeout = 200; // 2s

		rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &disc.addr,
							 10000, &connParams, Ble::gapEventCb, NULL);
		if (rc != 0) {
			ESP_LOGE(TAG, "连接手柄失败: %d", rc);
			Ble::instance().startScan();
		}
		return 0;
	}
	return 0;
}

// ── GAP 事件回调 ──────────────────────────────────────

int Ble::gapEventCb(struct ble_gap_event* event, void* arg)
{
	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		if (event->connect.status == 0) {
			auto ch = event->connect.conn_handle;
			ESP_LOGI(TAG, "手柄已连接, conn_handle=%d", ch);
			// 由 GamepadClient 处理后续发现流程
			GamepadClient::instance().onConnected(ch);
		} else {
			ESP_LOGE(TAG, "连接失败, status=%d", event->connect.status);
			// 继续扫描
			Ble::instance().startScan();
		}
		return 0;

	case BLE_GAP_EVENT_DISCONNECT:
	{
		auto ch = event->disconnect.conn.conn_handle;
		auto pid = GamepadManager::instance().getPlayerId(ch);
		ESP_LOGI(TAG, "手柄已断开 (Player=%d, conn=%d, reason=%d)",
				 pid, ch, event->disconnect.reason);
		GamepadManager::instance().releaseSlot(ch);
		GamepadClient::instance().onDisconnected(ch);
		// 继续扫描
		Ble::instance().startScan();
		return 0;
	}

	case BLE_GAP_EVENT_NOTIFY_RX:
	{
		// 手柄发来的通知（输入数据）
		uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
		if (len >= 6) {
			uint8_t buf[6];
			ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), nullptr);
			GamepadManager::instance().updateInput(
				event->notify_rx.conn_handle, buf, len);
		}
		return 0;
	}

	case BLE_GAP_EVENT_MTU:
		ESP_LOGD(TAG, "MTU 更新: %d", event->mtu.value);
		return 0;

	default:
		return 0;
	}
}

// ── 同步回调 ──────────────────────────────────────────

void Ble::syncCb(void)
{
	ESP_LOGI(TAG, "NimBLE 同步成功，开始扫描手柄...");
	Ble::instance().startScan();
}

void Ble::resetCb(int reason)
{
	ESP_LOGW(TAG, "NimBLE 重置, reason=%d", reason);
}

// ── Host 任务 ───────────────────────────────────────────

void Ble::hostTask(void* param)
{
	ESP_LOGI(TAG, "NimBLE Host 任务已启动");
	nimble_port_run();
	nimble_port_freertos_deinit();
}

// ── 公开接口 ────────────────────────────────────────────

void Ble::startScan()
{
	if (scanning) return;
	scanning = true;

	struct ble_gap_disc_params discParams;
	std::memset(&discParams, 0, sizeof(discParams));
	discParams.itvl = 40;       // 扫描间隔
	discParams.window = 20;     // 扫描窗口
	discParams.passive = 0;     // 主动扫描（0=主动，1=被动）
	discParams.filter_policy = 0;
	discParams.limited = 0;

	int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
						  &discParams, discCb, nullptr);
	if (rc == 0) {
		ESP_LOGI(TAG, "开始扫描手柄...");
	} else {
		ESP_LOGE(TAG, "扫描启动失败: %d", rc);
		scanning = false;
	}
}

bool Ble::start()
{
	if (started) return true;

	ESP_LOGI(TAG, "初始化 NimBLE Host (Central)...");
	s_instance = this;

	// 1. 初始化 C6 的 BT 控制器
	esp_err_t ret = esp_hosted_bt_controller_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "BT 控制器初始化失败: %s", esp_err_to_name(ret));
		return false;
	}
	ret = esp_hosted_bt_controller_enable();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "BT 控制器启用失败: %s", esp_err_to_name(ret));
		return false;
	}

	// 2. 初始化 NimBLE Host
	ret = nimble_port_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "NimBLE 初始化失败: %s", esp_err_to_name(ret));
		return false;
	}

	// 3. 配置主机回调
	ble_hs_cfg.sync_cb = syncCb;
	ble_hs_cfg.reset_cb = resetCb;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	// 4. 初始化 BLE 存储
	ble_store_config_init();

	// 5. 启动 NimBLE Host 任务
	nimble_port_freertos_init(hostTask);

	started = true;
	ESP_LOGI(TAG, "NimBLE Host 初始化完成");
	return true;
}

void Ble::stop()
{
	if (!started) return;
	ESP_LOGI(TAG, "停止 BLE...");
	ble_gap_disc_cancel();
	nimble_port_stop();
	nimble_port_deinit();
	started = false;
	scanning = false;
	s_instance = nullptr;
}
