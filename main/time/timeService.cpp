#include "timeService.hpp"

#include "wifi/wifi.hpp"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>

static constexpr char TAG[] = "TimeService";

// ── NVS ──
static constexpr char NvsNamespace[] = "time";
static constexpr char NvsKeyAutoSync[] = "auto_sync";

// ════════════════════════════════════════════════════════════════
// 单例
// ════════════════════════════════════════════════════════════════

TimeService& TimeService::instance()
{
	static TimeService s;
	return s;
}

// ════════════════════════════════════════════════════════════════
// 初始化
// ════════════════════════════════════════════════════════════════

void TimeService::start()
{
	if (m_started) return;
	m_started = true;

	// 1. 读 NVS 自动同步开关（默认开启）
	nvs_handle handle;
	if (nvs_open(NvsNamespace, NVS_READONLY, &handle) == ESP_OK)
	{
		uint8_t v = 1;
		if (nvs_get_u8(handle, NvsKeyAutoSync, &v) != ESP_OK)
			v = 1;
		m_autoSync = (v != 0);
		nvs_close(handle);
	}
	ESP_LOGI(TAG, "自动同步: %s", m_autoSync ? "ON" : "OFF");

	// 2. 注册 WiFi 拿到 IP 事件（自动模式下触发立即同步）
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onGotIpEvent, nullptr));

	// 4. 初始化 SNTP（全局唯一一次；静态服务器，无需 DHCP renew）
	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	config.start = true;                       // 初始化后立即启动
	config.server_from_dhcp = false;           // 固定使用 pool.ntp.org
	config.wait_for_sync = false;              // 不使用 sync_wait，省资源
	config.sync_cb = &onTimeSyncCb;            // 同步完成回调（记录上次同步时刻 + 通知）
	config.renew_servers_after_new_IP = false; // 静态服务器无需 renew；由 onGotIpEvent 控制开关
	esp_err_t err = esp_netif_sntp_init(&config);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "SNTP 初始化失败: %s", esp_err_to_name(err));

	ESP_LOGI(TAG, "SNTP 服务启动完成");
}

// ════════════════════════════════════════════════════════════════
// 自动同步开关
// ════════════════════════════════════════════════════════════════

bool TimeService::isAutoSync() const
{
	return m_autoSync;
}

void TimeService::setAutoSync(bool enable)
{
	if (m_autoSync == enable) return;
	m_autoSync = enable;

	// 持久化到 NVS
	nvs_handle handle;
	if (nvs_open(NvsNamespace, NVS_READWRITE, &handle) == ESP_OK)
	{
		nvs_set_u8(handle, NvsKeyAutoSync, enable ? 1 : 0);
		nvs_commit(handle);
		nvs_close(handle);
	}

	ESP_LOGI(TAG, "自动同步: %s", enable ? "ON" : "OFF");

	// 开启时若 WiFi 已连接，立即同步一次
	if (enable && wifiIsConnect())
		forceSync();
}

// ════════════════════════════════════════════════════════════════
// 手动同步
// ════════════════════════════════════════════════════════════════

void TimeService::forceSync()
{
	if (!m_started) return;

	// esp_netif_sntp_start() 在已启动时会 stop + init 重启，立即重新发起 NTP 请求（幂等）
	esp_err_t err = esp_netif_sntp_start();
	if (err == ESP_OK)
		ESP_LOGI(TAG, "SNTP 重新同步已触发");
	else
		ESP_LOGW(TAG, "SNTP 重启失败: %s", esp_err_to_name(err));
}

// ════════════════════════════════════════════════════════════════
// 状态查询
// ════════════════════════════════════════════════════════════════

bool TimeService::isSynced() const
{
	time_t now = time(nullptr);
	struct tm t{};
	localtime_r(&now, &t);
	return t.tm_year > 70;
}

time_t TimeService::getLastSyncUnix() const
{
	return m_lastSyncUnix;
}

// ════════════════════════════════════════════════════════════════
// 同步完成回调（多注册 + 定点清除）
// ════════════════════════════════════════════════════════════════

TimeService::SyncCbHandle TimeService::addSyncCb(SyncCallback cb)
{
	Lock lock(m_syncCbMutex);
	SyncCbHandle h = ++m_nextSyncCbHandle;
	if (h == 0) h = ++m_nextSyncCbHandle; // 跳过 0（保留作无效句柄）
	m_syncCbs.emplace_back(h, std::move(cb));
	return h;
}

void TimeService::removeSyncCb(SyncCbHandle handle)
{
	if (handle == 0) return;
	Lock lock(m_syncCbMutex);
	auto it = std::remove_if(m_syncCbs.begin(), m_syncCbs.end(),
		[handle](const std::pair<SyncCbHandle, SyncCallback>& p)
		{ return p.first == handle; });
	m_syncCbs.erase(it, m_syncCbs.end());
}

void TimeService::notifySyncDone()
{
	// 持锁直接调用（不拷贝）：与 removeSyncCb 互斥 → 回调执行时注册者必然存活。
	// 约定：回调体内不得再调用本类方法（防非递归互斥锁死锁），应保持轻量。
	Lock lock(m_syncCbMutex);
	for (auto& p : m_syncCbs)
	{
		if (p.second) p.second();
	}
}

// ════════════════════════════════════════════════════════════════
// 事件回调
// ════════════════════════════════════════════════════════════════

void TimeService::onTimeSyncCb(struct timeval* tv)
{
	auto& self = instance();
	if (tv)
		self.m_lastSyncUnix = tv->tv_sec;
	ESP_LOGI(TAG, "时间同步成功 (unix=%lld)", (long long)self.m_lastSyncUnix);
	self.notifySyncDone();
}

void TimeService::onGotIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data)
{
	(void)arg; (void)base; (void)id; (void)data;
	auto& self = instance();
	if (self.m_autoSync)
	{
		ESP_LOGI(TAG, "WiFi 已连接，触发时间同步");
		self.forceSync();
	}
}
