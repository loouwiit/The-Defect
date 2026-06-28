#include "bleGamepad.hpp"

#include "app/app.hpp"
#include "esp_log.h"
#include "esp_err.h"

// ESP HOSTED
#include "esp_hosted.h"

// NimBLE
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"

// NVS
#include "nvs_flash.h"
#include "nvs.h"

// ble_store_config_init 在 nimble/ble_store.h 中声明
// 但该头文件不在标准包含路径中，这里前置声明
#ifdef __cplusplus
extern "C" {
#endif
	void ble_store_config_init(void);
#ifdef __cplusplus
}
#endif

static constexpr char TAG[] = "BleGamepad";
static constexpr int INPUT_QUEUE_LEN = 16;

BleGamepad* BleGamepad::s_instance = nullptr;

// ── Singleton ──
BleGamepad& BleGamepad::instance()
{
	static BleGamepad inst;
	return inst;
}

// ── 回调转发 ──

// NimBLE 同步回调
void BleGamepad::onSync()
{
    ESP_LOGI(TAG, "NimBLE synced");
    instance().autoConnectPaired();
}

void BleGamepad::onReset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
}

// ── NVS 持久化 ──

void BleGamepad::syncPairedToNvs()
{
    // 收集当前已连接的设备 (BDA + name)
    std::vector<PairedDevice> connectedDevices;
    for (int i = 0; i < MaxPlayers; i++)
    {
        if (m_devices[i].connected)
        {
            PairedDevice pd;
            memcpy(pd.bda, m_devices[i].bda, 6);
            strncpy(pd.name, m_devices[i].name, sizeof(pd.name) - 1);
            pd.name[sizeof(pd.name) - 1] = '\0';
            connectedDevices.push_back(pd);
        }
    }

    nvs_handle handle;
    esp_err_t err = nvs_open("ble_gamepad", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return;
    }

    if (connectedDevices.empty())
    {
        err = nvs_erase_key(handle, "paired");
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW(TAG, "NVS erase failed: %d", err);
    }
    else
    {
        err = nvs_set_blob(handle, "paired", connectedDevices.data(),
                           connectedDevices.size() * sizeof(PairedDevice));
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "NVS set blob failed: %d", err);
            nvs_close(handle);
            return;
        }
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "NVS commit failed: %d", err);

    nvs_close(handle);

    ESP_LOGI(TAG, "配对状态已保存 (%zu 台设备)", connectedDevices.size());
}

void BleGamepad::loadPairedFromNvs()
{
    m_pairedDevices.clear();

    nvs_handle handle;
    esp_err_t err = nvs_open("ble_gamepad", NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        if (err != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGE(TAG, "NVS open failed: %d", err);
        return;
    }

    size_t blobSize = 0;
    err = nvs_get_blob(handle, "paired", nullptr, &blobSize);
    if (err != ESP_OK || blobSize == 0)
    {
        nvs_close(handle);
        return;
    }

    size_t count = blobSize / sizeof(PairedDevice);
    m_pairedDevices.resize(count);

    err = nvs_get_blob(handle, "paired", m_pairedDevices.data(), &blobSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS get blob failed: %d", err);
        m_pairedDevices.clear();
    }
    else
    {
        ESP_LOGI(TAG, "从 NVS 加载了 %zu 个配对设备", m_pairedDevices.size());
    }

    nvs_close(handle);
}

std::vector<PairedDevice> BleGamepad::getPairedDevices() const
{
    return m_pairedDevices;
}

// ── 自动重连 (scan-then-connect) ──

int BleGamepad::autoScanCb(struct ble_gap_event* event, void* /*arg*/)
{
    auto& self = instance();

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        // 检查是否 HID 设备
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0)
            return 0;

        bool isHid = false;
        if (fields.appearance_is_present)
            isHid = (fields.appearance >= 0x03C0 && fields.appearance <= 0x03C9);
        if (!isHid && fields.num_uuids16 > 0)
        {
            for (int i = 0; i < fields.num_uuids16; i++)
            {
                if (fields.uuids16[i].value == 0x1812)
                {
                    isHid = true;
                    break;
                }
            }
        }
        if (!isHid) return 0;

        // 检查 BDA 是否在配对列表中
        BdaBuffer foundBda;
        memcpy(foundBda.data(), event->disc.addr.val, 6);

        for (auto& paired : self.m_pairedDevices)
        {
            if (memcmp(paired.bda, foundBda.data(), 6) == 0)
            {
                // 去重
                bool alreadyFound = false;
                for (const auto& f : self.m_foundBdas)
                {
                    if (memcmp(f.data(), foundBda.data(), 6) == 0)
                    {
                        alreadyFound = true;
                        break;
                    }
                }
                if (!alreadyFound)
                {
                    self.m_foundBdas.push_back(foundBda);

                    // 从广告中更新 name
                    if (fields.name_len > 0)
                    {
                        size_t cpLen = std::min<size_t>(fields.name_len, sizeof(paired.name) - 1);
                        memcpy(paired.name, fields.name, cpLen);
                        paired.name[cpLen] = '\0';
                    }

                    ESP_LOGI(TAG, "Auto-connect 发现配对设备: %s [%02x:%02x:%02x:%02x:%02x:%02x]",
                            paired.name,
                            foundBda[0], foundBda[1], foundBda[2],
                            foundBda[3], foundBda[4], foundBda[5]);
                }
                break;
            }
        }
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
    {
        ESP_LOGI(TAG, "Auto-connect 扫描完成，找到 %zu 个配对设备", self.m_foundBdas.size());

        // 对找到的配对设备发起连接
        for (const auto& bda : self.m_foundBdas)
        {
            // 检查是否已连接
            bool alreadyConnected = false;
            for (int i = 0; i < MaxPlayers; i++)
            {
                if (self.m_devices[i].connected &&
                    memcmp(self.m_devices[i].bda, bda.data(), 6) == 0)
                {
                    alreadyConnected = true;
                    break;
                }
            }
            if (alreadyConnected) continue;

            // 构造 ScanDevice，从配对列表中取真实 name
            ScanDevice dev;
            memcpy(dev.bda, bda.data(), 6);
            dev.addrType = BLE_ADDR_PUBLIC;
            dev.name[0] = '\0';
            for (auto& pd : self.m_pairedDevices)
            {
                if (memcmp(pd.bda, bda.data(), 6) == 0)
                {
                    strncpy(dev.name, pd.name, sizeof(dev.name) - 1);
                    break;
                }
            }
            if (dev.name[0] == '\0')
                snprintf(dev.name, sizeof(dev.name), "HID_%02X%02X%02X",
                         bda[3], bda[4], bda[5]);
            dev.rssi = 0;
            dev.appearance = 0;

            ESP_LOGI(TAG, "Auto-connect 到 %s [%02x:%02x:%02x:%02x:%02x:%02x]",
                     dev.name,
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            self.connectDirect(dev);
        }

        self.m_foundBdas.clear();
        break;
    }

    default:
        break;
    }
    return 0;
}

void BleGamepad::autoConnectPaired()
{
    // 从 NVS 加载配对列表
    loadPairedFromNvs();

    if (m_pairedDevices.empty())
    {
        ESP_LOGI(TAG, "无配对设备，跳过自动重连");
        return;
    }

    ESP_LOGI(TAG, "开始自动重连，%zu 个配对设备", m_pairedDevices.size());

    // 清空上次扫描结果
    m_foundBdas.clear();

    // 启动 5 秒扫描
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "auto-connect: ble_hs_id_infer_auto failed: %d", rc);
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    struct ble_gap_disc_params params{};
    params.passive = 1;
    params.itvl = 0;
    params.window = 0;
    params.filter_duplicates = 1;

    rc = ble_gap_disc(own_addr_type, pdMS_TO_TICKS(5000), &params, autoScanCb, nullptr);
    if (rc != 0)
        ESP_LOGE(TAG, "auto-connect: scan start failed: %d", rc);
    else
        ESP_LOGI(TAG, "auto-connect: 扫描 5 秒...");
}

// NimBLE GAP 事件（扫描发现）
int BleGamepad::scanGapEventCb(struct ble_gap_event* event, void* /*arg*/)
{
	auto& self = BleGamepad::instance();

	switch (event->type) {
	case BLE_GAP_EVENT_DISC:
	{
		// 检查是否为 HID 设备 — 找 HID Service UUID (0x1812) 或游戏手柄 appearance
		struct ble_hs_adv_fields fields;
		if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0)
			return 0;

		bool isHid = false;
		if (fields.appearance_is_present) {
			// 0x03C4 = HID Gamepad
			isHid = (fields.appearance >= 0x03C0 && fields.appearance <= 0x03C9);
		}
		if (!isHid && fields.num_uuids16 > 0) {
			for (int i = 0; i < fields.num_uuids16; i++) {
				if (fields.uuids16[i].value == 0x1812) { // HID Service
					isHid = true;
					break;
				}
			}
		}
		if (!isHid) return 0;

		// 收集设备信息
		ScanDevice dev;
		memcpy(dev.bda, event->disc.addr.val, 6);
		dev.addrType = event->disc.addr.type;
		dev.rssi = event->disc.rssi;
		dev.appearance = fields.appearance;
		if (fields.name_len > 0) {
			size_t cpLen = std::min<size_t>(fields.name_len, sizeof(dev.name) - 1);
			memcpy(dev.name, fields.name, cpLen);
			dev.name[cpLen] = '\0';
		}
		else {
			snprintf(dev.name, sizeof(dev.name), "HID_%02X%02X%02X",
				dev.bda[3], dev.bda[4], dev.bda[5]);
		}

		{
			if (self.m_mutex.try_lock()) {
				// 去重: 相同 BDA 不重复添加
				bool dup = false;
				for (auto& d : self.m_scanResults) {
					if (memcmp(d.bda, dev.bda, 6) == 0) {
						d.rssi = dev.rssi; // 更新 RSSI
						dup = true;
						break;
					}
				}
				if (!dup) self.m_scanResults.push_back(dev);
				self.m_mutex.unlock();
			}
		}
		ESP_LOGI(TAG, "SCAN: %s [%02x:%02x:%02x:%02x:%02x:%02x], RSSI=%d, appearance=0x%04x",
			dev.name,
			dev.bda[0], dev.bda[1], dev.bda[2], dev.bda[3], dev.bda[4], dev.bda[5],
			dev.rssi, dev.appearance);
		break;
	}
	case BLE_GAP_EVENT_DISC_COMPLETE:
	{
		ESP_LOGI(TAG, "Scan complete, found %zu devices", self.m_scanResults.size());
		// 扫描自动重新开始（持续模式）
		if (self.m_scanning) {
			uint8_t own_addr_type;
			ble_hs_id_infer_auto(0, &own_addr_type);
			struct ble_gap_disc_params params{};
			params.passive = 1;
			params.filter_duplicates = 1;
ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, scanGapEventCb, nullptr);
		}
		break;
	}
	default:
		break;
	}
	return 0;
}

// ── NimBLE Host 任务 ──
void BleGamepad::hostTask(void* /*arg*/)
{
	ESP_LOGI(TAG, "NimBLE host task started");
	nimble_port_run(); // 阻塞直到 nimble_port_stop()
	nimble_port_freertos_deinit();
}

// ── 输入处理任务 ──
void BleGamepad::processTask(void* arg)
{
	auto& self = *static_cast<BleGamepad*>(arg);
	GamepadInputEvent evt;

	while (true) {
		if (xQueueReceive(self.m_inputQueue, &evt, portMAX_DELAY) == pdTRUE) {
			// 更新设备状态
			if (evt.playerId < MaxPlayers) {
				auto& ctx = self.m_devices[evt.playerId];
				ctx.state = evt.state;
				// 状态变化日志（调试用）
				if (ctx.state.buttons || ctx.state.dpad < 8) {
					ESP_LOGD(TAG, "P%d: btn=0x%04x lx=%d ly=%d rx=%d ry=%d dpad=%d",
						evt.playerId, evt.state.buttons,
						evt.state.lx, evt.state.ly,
						evt.state.rx, evt.state.ry,
						evt.state.dpad);
				}
				// 路由到当前活跃 App
				if (self.m_display) {
					auto* app = self.m_display->getActiveApp();
					if (app) app->onGamepadInput(evt.playerId, evt.state);
				}
			}
		}
	}
}

// ── 初始化 ──

bool BleGamepad::initEspHostedBt()
{
	ESP_LOGI(TAG, "Connecting to co-processor...");
	if (esp_hosted_connect_to_slave() != ESP_OK) {
		ESP_LOGE(TAG, "Failed to connect to slave");
		return false;
	}

	ESP_LOGI(TAG, "Initializing BT controller...");
	if (esp_hosted_bt_controller_init() != ESP_OK) {
		ESP_LOGE(TAG, "BT controller init failed");
		return false;
	}

	ESP_LOGI(TAG, "Enabling BT controller...");
	if (esp_hosted_bt_controller_enable() != ESP_OK) {
		ESP_LOGE(TAG, "BT controller enable failed");
		return false;
	}

	return true;
}

bool BleGamepad::initNimble()
{
	ESP_LOGI(TAG, "Initializing NimBLE...");
	nimble_port_init();

	// 设置 NimBLE 回调
	ble_hs_cfg.reset_cb = onReset;
	ble_hs_cfg.sync_cb = onSync;

	ble_store_config_init();
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	// 创建输入事件队列
	m_inputQueue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(GamepadInputEvent));
	if (!m_inputQueue) {
		ESP_LOGE(TAG, "Failed to create input queue");
		return false;
	}

	// 创建输入处理任务
	BaseType_t res = xTaskCreatePinnedToCore(
		processTask, "bleGamepad", 4096, this, 5, &m_processTask,
		tskNO_AFFINITY);
	if (res != pdPASS) {
		ESP_LOGE(TAG, "Failed to create process task");
		return false;
	}

	// 启动 NimBLE 任务
	nimble_port_freertos_init(hostTask);

	return true;
}

bool BleGamepad::start(Display* display)
{
	if (m_running) return true;

	s_instance = this;
	m_display = display;

	if (!initEspHostedBt()) {
		ESP_LOGE(TAG, "ESP HOSTED BT init failed");
		return false;
	}

	if (!initNimble()) {
		ESP_LOGE(TAG, "NimBLE init failed");
		return false;
	}

	m_running = true;
	ESP_LOGI(TAG, "BleGamepad started");
	return true;
}

void BleGamepad::stop()
{
	if (!m_running) return;

	disconnectAll();

	if (m_processTask) {
		vTaskDelete(m_processTask);
		m_processTask = nullptr;
	}
	if (m_inputQueue) {
		vQueueDelete(m_inputQueue);
		m_inputQueue = nullptr;
	}

	nimble_port_stop();

	esp_hosted_bt_controller_disable();
	esp_hosted_bt_controller_deinit(false);

	m_running = false;
	s_instance = nullptr;
	ESP_LOGI(TAG, "BleGamepad stopped");
}

// ── 扫描 ──

void BleGamepad::startScan()
{
	if (!m_running || m_scanning) return;
	m_scanning = true;

	{
		if (m_mutex.try_lock()) {
			m_scanResults.clear();
			m_mutex.unlock();
		}
	}

	// 获取正确的地址类型
	uint8_t own_addr_type;
	int rc = ble_hs_id_infer_auto(0, &own_addr_type);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
		own_addr_type = BLE_OWN_ADDR_PUBLIC;
	}

	struct ble_gap_disc_params params{};
	params.filter_policy = 0;
	params.passive = 1;
	params.itvl = 0;
	params.window = 0;
	params.filter_duplicates = 1;  // 去重，减少刷屏

	rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, scanGapEventCb, nullptr);
	if (rc == 0) {
		ESP_LOGI(TAG, "Scan started (own_addr_type=%d)", own_addr_type);
	}
	else {
		ESP_LOGE(TAG, "Scan start failed: %d (%s)", rc,
			rc == BLE_HS_EBUSY ? "BUSY" :
			rc == BLE_HS_ENOTSYNCED ? "NOT SYNCED" :
			rc == BLE_HS_EALREADY ? "ALREADY" : "OTHER");
		m_scanning = false;
	}
}

void BleGamepad::stopScan()
{
	if (!m_scanning) return;
	m_scanning = false;
	ble_gap_disc_cancel();
	ESP_LOGI(TAG, "Scan stopped");
}

// ── 连接管理 ──

void BleGamepad::connect(uint8_t scanIndex)
{
	ScanDevice dev;
	{
		if (!m_mutex.try_lock()) return;
		if (scanIndex >= m_scanResults.size()) {
			m_mutex.unlock();
			ESP_LOGE(TAG, "Invalid scan index %u", scanIndex);
			return;
		}
		dev = m_scanResults[scanIndex];
		m_mutex.unlock();
	}

	ESP_LOGI(TAG, "Connecting to %s [%02x:%02x:%02x:%02x:%02x:%02x] addr_type=%d...",
		dev.name,
		dev.bda[0], dev.bda[1], dev.bda[2], dev.bda[3], dev.bda[4], dev.bda[5],
		dev.addrType);

	// 检查是否已连
	for (int i = 0; i < MaxPlayers; i++) {
		if (m_devices[i].connected && memcmp(m_devices[i].bda, dev.bda, 6) == 0) {
			ESP_LOGW(TAG, "Already connected to this device");
			return;
		}
	}

	// 连接前停止扫描
	stopScan();
	connectDirect(dev);
}

bool BleGamepad::connectDirect(const ScanDevice& dev)
{
	// 使用原始 NimBLE GAP 连接
	ble_addr_t addr;
	memcpy(addr.val, dev.bda, 6);
	addr.type = dev.addrType;

	uint8_t own_addr_type;
	ble_hs_id_infer_auto(0, &own_addr_type);

	int rc = ble_gap_connect(own_addr_type, &addr, 30000, NULL,
		connectGapEvent, nullptr);
	if (rc != 0) {
		ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
		startScan();
		return false;
	}
	return true;
}

// 订阅 CCCD 写回调
static int subWriteCb(uint16_t conn_handle, const struct ble_gatt_error* error,
	struct ble_gatt_attr* attr, void* arg)
{
	if (error->status == 0)
		ESP_LOGI(TAG, "    -> Subscribed! Ready for HID input.");
	else
		ESP_LOGE(TAG, "    -> Subscribe failed: %d", error->status);
	return 0;
}

// HID 服务特征发现回调
static int hidChrDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
	const struct ble_gatt_chr* chr, void* arg)
{
	if (error->status == 0 && chr) {
		uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
		ESP_LOGI(TAG, "    CHAR: val=%u uuid=0x%04x props=0x%02x",
			chr->val_handle, uuid, chr->properties);

		// HID Input Report (0x2A4D) — 订阅通知
		if (uuid == 0x2A4D && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
			ESP_LOGI(TAG, "    -> HID Input Report! Subscribing at handle %u...", chr->val_handle + 1);
			uint8_t val[] = { 0x01, 0x00 };
			ble_gattc_write_flat(conn_handle, chr->val_handle + 1, val, sizeof(val),
				subWriteCb, nullptr);
		}
	}
	else if (error->status == BLE_HS_EDONE) {
		ESP_LOGI(TAG, "  HID characteristic discovery complete");
	}
	else if (error->status != 0) {
		ESP_LOGE(TAG, "  HID char discovery error: %d", error->status);
	}
	return 0;
}

// 服务发现回调
struct DiscoveredService {
	uint16_t start, end, uuid;
};
static DiscoveredService s_svcList[20];
static int s_svcCount = 0;

static int svcDiscCb(uint16_t conn_handle, const struct ble_gatt_error* error,
	const struct ble_gatt_svc* service, void* arg)
{
	if (error->status == 0 && service) {
		uint16_t uuid = ble_uuid_u16(&service->uuid.u);
		ESP_LOGI(TAG, "  Service: start=%u end=%u uuid=0x%04x",
			service->start_handle, service->end_handle, uuid);
		if (s_svcCount < 20) {
			s_svcList[s_svcCount++] = { service->start_handle, service->end_handle, uuid };
		}
	}
	else if (error->status == BLE_HS_EDONE) {
		ESP_LOGI(TAG, "  Service discovery complete");

		// 找到 HID 服务 (0x1812)，发现其特征
		for (int i = 0; i < s_svcCount; i++) {
			if (s_svcList[i].uuid == 0x1812) {
				ESP_LOGI(TAG, "  -> Discovering HID service characteristics...");
				ble_gattc_disc_all_chrs(conn_handle, s_svcList[i].start, s_svcList[i].end,
					hidChrDiscCb, nullptr);
				break;
			}
		}
	}
	else if (error->status != 0) {
		ESP_LOGE(TAG, "  Service discovery error: %d", error->status);
	}
	return 0;
}

int BleGamepad::connectGapEvent(struct ble_gap_event* event, void* /*arg*/)
{
	auto& self = instance();
	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
	{
		if (event->connect.status != 0) {
			ESP_LOGE(TAG, "Connect failed: %d", event->connect.status);
			self.startScan();
			return 0;
		}
		uint16_t connHandle = event->connect.conn_handle;

		// 通过 conn_handle 获取连接描述符，得到对端地址
		struct ble_gap_conn_desc desc;
		if (ble_gap_conn_find(connHandle, &desc) == 0) {
			ESP_LOGI(TAG, "Connected! handle=%d peer=%02x:%02x:%02x:%02x:%02x:%02x",
				connHandle,
				desc.peer_id_addr.val[0], desc.peer_id_addr.val[1],
				desc.peer_id_addr.val[2], desc.peer_id_addr.val[3],
				desc.peer_id_addr.val[4], desc.peer_id_addr.val[5]);
			// 分配 playerId 并记录设备
			int pid = self.allocPlayerId();
			if (pid >= 0) {
				auto& ctx = self.m_devices[pid];
				ctx.connHandle = connHandle;
				ctx.playerId = pid;
				ctx.connected = true;
				memcpy(ctx.bda, desc.peer_id_addr.val, 6);
				// 从扫描结果中查找设备名
				ctx.name[0] = '\0';
				for (auto& sd : self.m_scanResults) {
					if (memcmp(sd.bda, desc.peer_id_addr.val, 6) == 0) {
						strncpy(ctx.name, sd.name, sizeof(ctx.name) - 1);
						break;
					}
				}
				// Auto-connect 回退: 从配对列表中取 name
				if (ctx.name[0] == '\0') {
					for (auto& pd : self.m_pairedDevices) {
						if (memcmp(pd.bda, desc.peer_id_addr.val, 6) == 0) {
							strncpy(ctx.name, pd.name, sizeof(ctx.name) - 1);
							break;
						}
					}
				}
				ESP_LOGI(TAG, "Connected: assigned playerId=%d, name=%s", pid, ctx.name);
			}
		}

		// 发现所有服务并打印
		int rc = ble_gattc_disc_all_svcs(connHandle, svcDiscCb, nullptr);
		if (rc != 0) {
			ESP_LOGE(TAG, "Failed to start service discovery: %d", rc);
		}
		break;
	}
	case BLE_GAP_EVENT_DISCONNECT:
	{
		uint16_t handle = event->disconnect.conn.conn_handle;
		ESP_LOGI(TAG, "Disconnected: handle=%d, reason=%d", handle, event->disconnect.reason);
		// 精确匹配 conn_handle 清理设备上下文
		for (int i = 0; i < MaxPlayers; i++) {
			if (self.m_devices[i].connected && self.m_devices[i].connHandle == handle) {
				ESP_LOGI(TAG, "Clearing device playerId=%d", i);
				self.m_devices[i] = {};
				break;
			}
		}
		break;
	}
	case BLE_GAP_EVENT_NOTIFY_RX:
	{
		// 手柄发来的 4 字节 HID 输入报告
		const struct os_mbuf* om = event->notify_rx.om;
		uint16_t total = OS_MBUF_PKTLEN(om);
		if (total < 4)
		{
			ESP_LOGW(TAG, "Received too short HID input report (%d bytes)", total);
			break; // 至少 4 字节
		}
		uint8_t report[64];
		uint16_t len = total > sizeof(report) ? sizeof(report) : total;
		ble_hs_mbuf_to_flat(om, report, len, NULL);

		// 解析 4 字节 HID 报告 → GamepadState
		// 格式: [buttons:u16][x:int8][y:int8]
		GamepadState state{};
		state.buttons = report[0] | (uint16_t(report[1]) << 8);
		// int8 → uint8 (偏移128, 使中心=128)
		state.lx = (uint8_t)((int8_t)report[2] + 128);
		state.ly = (uint8_t)((int8_t)report[3] + 128);

		// 找到对应的 playerId
		int pid = -1;
		for (int i = 0; i < MaxPlayers; i++) {
			if (self.m_devices[i].connected && self.m_devices[i].connHandle == event->notify_rx.conn_handle) {
				pid = i;
				break;
			}
		}
		if (pid < 0) break;

		// 入队
		GamepadInputEvent evt{ (uint8_t)pid, state };
		if (self.m_inputQueue) {
			xQueueSend(self.m_inputQueue, &evt, 0);
		}
		break;
	}
	default:
		break;
	}
	return 0;
}

void BleGamepad::disconnect(uint8_t playerId)
{
	if (playerId >= MaxPlayers) return;
	auto& ctx = m_devices[playerId];
	if (!ctx.connected || ctx.connHandle == BLE_HS_CONN_HANDLE_NONE) return;

	ESP_LOGI(TAG, "Disconnecting player %d, handle=%d", playerId, ctx.connHandle);
	ble_gap_terminate(ctx.connHandle, 0x13);
}

void BleGamepad::disconnectAll()
{
	for (int i = 0; i < MaxPlayers; i++) {
		if (m_devices[i].connected) disconnect(i);
	}
}

// ── 查询 ──

uint8_t BleGamepad::connectedCount() const
{
	uint8_t count = 0;
	for (int i = 0; i < MaxPlayers; i++) {
		if (m_devices[i].connected) count++;
	}
	return count;
}

const DeviceContext* BleGamepad::getDevice(uint8_t playerId) const
{
	if (playerId >= MaxPlayers) return nullptr;
	return &m_devices[playerId];
}

std::vector<ScanDevice> BleGamepad::getScannedDevices() const
{
	std::vector<ScanDevice> ret;
	if (m_mutex.try_lock()) {
		ret = m_scanResults;
		m_mutex.unlock();
	}
	return ret;
}

// ── 内部 ──

int BleGamepad::allocPlayerId()
{
	for (int i = 0; i < MaxPlayers; i++) {
		if (!m_devices[i].connected) return i;
	}
	return -1;
}

int BleGamepad::playerIdByConnHandle(uint16_t handle) const
{
	for (int i = 0; i < MaxPlayers; i++) {
		if (m_devices[i].connected && m_devices[i].connHandle == handle) return i;
	}
	return -1;
}
