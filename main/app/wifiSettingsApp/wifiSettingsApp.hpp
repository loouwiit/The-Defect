#pragma once

#include "app/app.hpp"
#include "lvgl.h"
#include <esp_wifi_types_generic.h>
#include <vector>
#include <cstdint>

/**
 * @brief WiFi 设置 App（左右分屏）
 *
 * 左侧 = AP 热点管理面板，右侧 = Station 扫描/连接面板。
 *
 * AP 面板（左侧）：
 *   - AP 开关 / SSID 编辑 / 密码编辑 / 认证模式循环 / 已连接客户端数 / NAT 自动启动开关 / 网关 IP / 主机名
 *   - AP 配置由 IDF WiFi 驱动自动持久化到 NVS（CONFIG_ESP_WIFI_NVS_ENABLED=y）
 *   - 客户端计数用只读 API esp_wifi_ap_get_sta_list()，不做 MAC 列表
 *   - NAT 开关仅反映"自动启动"标志（wifiNatIsAutoStart），不显示当前运行态
 *
 * STA 面板（右侧）：
 *   - WiFi 开关 / 扫描列表 / 连接信息（SSID/IP + 断开）
 */
class WifiSettingsApp final : public App
{
public:
	constexpr static char TAG[] = "WifiSettingsApp";

	WifiSettingsApp(Display* display);
	~WifiSettingsApp() override;

	/** @brief AI open_app 静态工厂（能力表用） */
	static App* newApp(Display* display, const cJSON* params) {
		(void)params;
		return new WifiSettingsApp(display);
	}

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	static constexpr int MaxAps = 32;
	static constexpr int ScanDuration = 10000;
	static constexpr int RefreshInterval = 1000;
	static constexpr int RowHight = 60;

	// ── 通用编辑弹窗类型 ──
	enum class EditDialogKind : uint8_t {
		StaPassword,  // 连接 WiFi 密码（带 SSID 副标题）
		ApSsid,       // AP SSID
		ApPassword,   // AP 密码
		Hostname,     // mDNS 主机名
	};

	// ── 顶栏 ──
	lv_obj_t* m_backBtn{};

	// ── 扫描控制 ──
	lv_obj_t* m_scanBtn{};
	lv_obj_t* m_scanBtnLabel{};
	bool m_scanActive{ false };
	TickType_t m_scanStartTick{};

	// ── WiFi(STA) 开关 ──
	lv_obj_t* m_wifiToggleBtn{};
	lv_obj_t* m_wifiToggleLabel{};

	// ── 扫描结果（本地冻结副本） ──
	std::vector<wifi_ap_record_t> m_scanResults;

	// ── STA UI 容器 ──
	lv_obj_t* m_scanListContainer{};
	std::vector<lv_obj_t*> m_scanRows;
	std::vector<lv_obj_t*> m_connectBtns;

	// ── AP 面板 ──
	lv_obj_t* m_apToggleBtn{};
	lv_obj_t* m_apToggleLabel{};
	lv_obj_t* m_apSsidLabel{};
	lv_obj_t* m_apPwdLabel{};
	lv_obj_t* m_apAuthLabel{};
	lv_obj_t* m_apClientLabel{};
	lv_obj_t* m_apNatLabel{};
	lv_obj_t* m_apGatewayLabel{};
	lv_obj_t* m_hostnameLabel{};
	std::vector<lv_obj_t*> m_apRows;  // 可聚焦 AP 项（0=开关,1=SSID,2=密码,3=认证,4=NAT,5=主机名）

	// ── STA 连接信息区域 ──
	lv_obj_t* m_infoCard{};
	lv_obj_t* m_infoIcon{};
	lv_obj_t* m_infoSsidLabel{};
	lv_obj_t* m_infoIpLabel{};
	lv_obj_t* m_disconnectBtn{};
	bool m_isConnected{ false };

	// ── 通用编辑弹窗 ──
	lv_obj_t* m_editDialog{};
	lv_obj_t* m_editTa{};
	lv_obj_t* m_editKeyboard{};
	lv_group_t* m_editGroup{};
	EditDialogKind m_editKind{ EditDialogKind::StaPassword };
	size_t m_pwdTargetIdx{ 0 };

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};

	// ── 手柄焦点导航 ──
	enum FocusGroup : int8_t {
		FOCUS_TITLE = 0,   // 顶栏：仅 返回
		FOCUS_AP,          // 左面板：AP 控制行
		FOCUS_STA_TITLE,   // 右面板头部：WiFi开关 / 扫描
		FOCUS_STA_LIST,    // 右面板：扫描列表
		FOCUS_STA_INFO,    // 右面板底部：断开
	};
	FocusGroup m_focusGroup{ FOCUS_TITLE };
	int8_t m_focusApIdx{ 0 };        // 0..5 AP 行
	int8_t m_focusStaTitleIdx{ 0 };  // 0=WiFi开关, 1=扫描
	int8_t m_focusListIdx{ 0 };      // 0..N-1 扫描列表
	int8_t m_focusInfoIdx{ 0 };      // 0=断开
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 内部方法 ──
	void buildUi();
	void refreshUi();
	void updateScanList();
	void updateConnectionInfo();
	void updateHostnameRow();
	void updateApPanel();
	void doScan();
	void doConnect(size_t scanIndex, const char* password);
	void doDisconnect();
	void toggleWifi();
	void doSetHostname(const char* hostname);

	// ── AP 面板操作 ──
	void toggleAp();
	void cycleAuthMode();
	void toggleNat();
	void doSetApSsid(const char* ssid);
	void doSetApPassword(const char* password);
	static const char* authModeName(wifi_auth_mode_t mode);

	// ── 通用编辑弹窗 ──
	void showEditDialog(EditDialogKind kind);
	void hideEditDialog();

	// ── 焦点导航 ──
	void applyFocus();
	void activateFocus();
	void navApUp();
	void navApDown();
	void navStaTitleLeft();
	void navStaTitleRight();
	void navStaTitleUp();
	void navStaTitleDown();
	void navListUp();
	void navListDown();
	void navInfoUp();
	void navInfoDown();

	// ── 回调（静态，C 兼容） ──
	static void timerCb(lv_timer_t* t);
	static void onScanBtnCb(lv_event_t* e);
	static void onWifiToggleCb(lv_event_t* e);
	static void onConnectBtnCb(lv_event_t* e);
	static void onDisconnectBtnCb(lv_event_t* e);
	static void onEditConfirmCb(lv_event_t* e);
	static void onEditCancelCb(lv_event_t* e);
	static void onBackBtnCb(lv_event_t* e);
	static void onApToggleCb(lv_event_t* e);
	static void onApSsidCb(lv_event_t* e);
	static void onApPwdCb(lv_event_t* e);
	static void onApAuthCb(lv_event_t* e);
	static void onApNatCb(lv_event_t* e);
	static void onHostnameRowCb(lv_event_t* e);

	// ── AI 能力 ──
	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;
};
