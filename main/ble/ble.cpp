#include "ble.hpp"

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

// ── 广播参数 ────────────────────────────────────────────

/** 广播名 — 手机 nRF Connect 扫描可见（<= 8 字符避免广播包超限） */
static constexpr const char* DEVICE_NAME = "ESP32P4";

/** 完整设备名（GAP Service 中可读） */
static constexpr const char* DEVICE_NAME_FULL = "ESP32P4 Game";

/** 广播间隔 (40ms, unit=0.625ms) */
static constexpr int ADV_INTERVAL_MS = 40;
static constexpr int ADV_INTERVAL = ADV_INTERVAL_MS * 1000 / 625;

// ── 单例 ────────────────────────────────────────────────

Ble& Ble::instance()
{
	static Ble inst;
	return inst;
}

// ── 回调 ────────────────────────────────────────────────

void Ble::syncCb(void)
{
	ESP_LOGI(TAG, "NimBLE 同步成功，注册 GATT 服务...");

	// 注册 GAP Service + 设置完整设备名（用于连接后读取）
	ble_svc_gap_init();
	ble_svc_gatt_init();
	ble_svc_gap_device_name_set(DEVICE_NAME_FULL);

	// ── 在此处注册自定义 GATT Services ──
	// Phase 3: GamepadService::init() 将被添加到这里
	// int rc = gamepad_service_init();
	// assert(rc == 0);

	// ── 开始广播 ──
	// 先配置广播数据，再启动
	struct ble_hs_adv_fields advFields;
	std::memset(&advFields, 0, sizeof(advFields));

	// 广播中包含 Device Name（完整）
	advFields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	advFields.name = (uint8_t*)DEVICE_NAME;
	advFields.name_len = std::strlen(DEVICE_NAME);
	advFields.name_is_complete = 1;

	int rc = ble_gap_adv_set_fields(&advFields);
	if (rc != 0) {
		ESP_LOGE(TAG, "广播字段设置失败: %d", rc);
		return;
	}

	// ── 启动可连接广播 ──
	struct ble_gap_adv_params advParams;
	std::memset(&advParams, 0, sizeof(advParams));
	advParams.conn_mode = BLE_GAP_CONN_MODE_UND;  // 可连接非定向
	advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;   // 通用可发现
	advParams.itvl_min = ADV_INTERVAL;
	advParams.itvl_max = ADV_INTERVAL;

	rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
						   &advParams, gapEventCb, NULL);
	if (rc == 0) {
		ESP_LOGI(TAG, "BLE 广播已启动: \"%s\"", DEVICE_NAME);
	} else {
		ESP_LOGE(TAG, "BLE 广播启动失败: %d", rc);
	}
}

void Ble::resetCb(int reason)
{
	ESP_LOGW(TAG, "NimBLE 重置, reason=%d", reason);
}

static void gattSvrRegisterCb(struct ble_gatt_register_ctxt* ctxt, void* arg)
{
	switch (ctxt->op) {
	case BLE_GATT_REGISTER_OP_SVC:
		ESP_LOGD(TAG, "GATT 服务已注册");
		break;
	default:
		break;
	}
}

int Ble::gapEventCb(struct ble_gap_event* event, void* arg)
{
	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		if (event->connect.status == 0) {
			ESP_LOGI(TAG, "手柄已连接, conn_handle=%d",
					 event->connect.conn_handle);
		} else {
			ESP_LOGE(TAG, "连接失败, status=%d", event->connect.status);
			// 连接失败后重新开始广播
			ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
							  NULL, gapEventCb, NULL);
		}
		return 0;

	case BLE_GAP_EVENT_DISCONNECT:
		ESP_LOGI(TAG, "手柄已断开, conn_handle=%d, reason=%d",
				 event->disconnect.conn.conn_handle,
				 event->disconnect.reason);
		// 断开后重新开始广播
		ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
						  NULL, gapEventCb, NULL);
		return 0;

	case BLE_GAP_EVENT_CONN_UPDATE:
		if (event->conn_update.status == 0) {
			ESP_LOGD(TAG, "连接参数更新成功, handle=%d",
					 event->conn_update.conn_handle);
		} else {
			ESP_LOGW(TAG, "连接参数更新失败, status=%d",
					 event->conn_update.status);
		}
		return 0;

	case BLE_GAP_EVENT_MTU:
		ESP_LOGD(TAG, "MTU 更新: %d", event->mtu.value);
		return 0;

	default:
		return 0;
	}
}

// ── Host 任务 ───────────────────────────────────────────

void Ble::hostTask(void* param)
{
	ESP_LOGI(TAG, "NimBLE Host 任务已启动");
	nimble_port_run();  // 阻塞，运行 NimBLE 事件循环
	nimble_port_freertos_deinit();
}

// ── 公开接口 ────────────────────────────────────────────

bool Ble::start()
{
	if (started) {
		ESP_LOGW(TAG, "BLE 已初始化");
		return true;
	}

	ESP_LOGI(TAG, "初始化 NimBLE Host...");

	s_instance = this;

	// 1. 通过 esp-hosted 初始化 C6 的 BT 控制器
	ESP_LOGI(TAG, "初始化 BT 控制器 (C6)...");
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
	ESP_LOGI(TAG, "BT 控制器已就绪");

	// 2. 初始化 NimBLE Host
	ret = nimble_port_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "NimBLE 初始化失败: %s", esp_err_to_name(ret));
		return false;
	}

	// 3. 配置主机回调
	ble_hs_cfg.sync_cb = syncCb;
	ble_hs_cfg.reset_cb = resetCb;
	ble_hs_cfg.gatts_register_cb = gattSvrRegisterCb;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	// 4. 初始化 BLE 存储（用于绑定信息）
	ble_store_config_init();

	// 5. 启动 NimBLE Host FreeRTOS 任务
	nimble_port_freertos_init(hostTask);

	started = true;
	ESP_LOGI(TAG, "NimBLE Host 初始化完成");
	return true;
}

void Ble::stop()
{
	if (!started) return;
	ESP_LOGI(TAG, "停止 BLE...");
	ble_gap_adv_stop();
	nimble_port_stop();
	nimble_port_deinit();
	started = false;
	s_instance = nullptr;
}
