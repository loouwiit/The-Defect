#pragma once

#include "app/app.hpp"
#include "aiAgent/aiProtocol.hpp"
#include "elfManager/elfManager.hpp"
#include <cstdint>
#include <ctime>
#include <vector>

class DesktopApp final : public App
{
public:
	constexpr static char TAG[] = "DesktopApp";

	DesktopApp(Display* display);
	~DesktopApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

	// ── AI 能力 ──
	std::string getAiContext() override;
	std::vector<AiAppTarget> getOpenableApps() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;

private:
	tm showedTime{};

	// ── 动态游戏卡片 ──
	// 顺序：ELF 插件卡片（扫描插入最前）→ 内置游戏 → 系统卡片
	enum class CardKind : uint8_t { Elf, System };   // 内置游戏已全部迁移 ELF（Builtin 已删）
	struct GameCard {
		CardKind kind;
		char displayName[64];   // 显示名（中文）
		char desc[128];         // 描述
		char icon[64];          // 图标路径
		char elfPath[64];       // kind==Elf：.so 完整路径
		char pluginName[32];    // kind==Elf：info->name（ASCII，AI open_app 键）
		int8_t index;           // kind==Builtin/System：分发 switch 索引
	};
	std::vector<GameCard> m_cards;      // 卡片数据表
	std::vector<lv_obj_t*> m_cardObjs;  // 对应卡片对象（与 m_cards 同序）

	static constexpr int SYSTEM_COUNT = 3;    // luaManager/market/testApp
	static constexpr int MAX_ELF_CARDS = 8;

	// ── ELF 扫描（后台渐进式） ──
	bool m_elfScanStarted = false;
	volatile bool m_elfScanStopped = false;

	void buildBuiltinCards();                      // 填充内置+系统卡片（init 时）
	lv_obj_t* createCardObject(int idx);           // 创建第 idx 张卡片对象（须持 LVGL 锁）
	void startElfScan();                           // 启动后台扫描 Task
	static TickType_t elfScanTask(void* param);    // 渐进式：占位 → 逐个 inspect → 更新
	void addElfCardPlaceholder(const char* fullPath);  // 快速占位卡片（插入最前）
	void updateElfCard(const char* elfPath, const ElfManager::ScanEntry& e); // 元数据就绪刷新（按 path 定位）

	// ── 焦点导航组 ──
	enum FocusGroup : int8_t {
		FOCUS_CARDS = 0,  // 游戏卡片行（左右切换卡片）
		FOCUS_BOTTOM = 1,  // 底部"开始游戏"按钮
		FOCUS_STATUS = 2,  // 顶栏状态图标
	};

	// 状态栏焦点索引（单一数据源：导航/回调/渲染共用，防止索引漂移）
	// 顺序与状态栏视觉顺序一致（亮度 → 多主机 → 电池）
	enum StatusIdx : int8_t {
		STATUS_TIME = 0,     // 时间（时间设置）
		STATUS_WIFI = 1,     // WiFi
		STATUS_BT = 2,       // 蓝牙
		STATUS_VOL = 3,      // 音量
		STATUS_BRIGHT = 4,   // 亮度
		STATUS_CONSOLE = 5,  // 多主机管理
		STATUS_BATTERY = 6,  // 电池（电源管理）
		STATUS_COUNT = 7,
	};
	FocusGroup m_focusGroup{ FOCUS_CARDS };
	int8_t m_focusCardsIdx{ 0 };      // 0..m_cards.size()-1
	int8_t m_focusStatusIdx{ 0 };     // 0=时间,1=WiFi,2=蓝牙,3=音量,4=亮度,5=多主机,6=电池（见 StatusIdx）
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};

	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;
	static constexpr TickType_t ACTION_DELAY = 500;

	// ── UI 对象成员 ──
	lv_obj_t* m_cardsRow{};              // 卡片 flex 容器
	// 卡片对象存于 m_cardObjs（动态，与 m_cards 同序）

	lv_obj_t* m_prevBtn{};
	lv_obj_t* m_nextBtn{};

	lv_obj_t* m_descLabel{};
	lv_obj_t* m_infoLabel{};
	lv_obj_t* m_startBtn{};

	lv_obj_t* m_timeLabel{};
	lv_obj_t* m_statusSpacer{};
	lv_timer_t* m_timeTimer{};

	lv_obj_t* m_wifiLabel{};
	lv_obj_t* m_bluetoothLabel{};
	lv_obj_t* m_volumeLabel{};
	lv_obj_t* m_brightnessLabel{};

	lv_obj_t* m_consoleLabel{};

	lv_obj_t* m_brightnessSlider{};
	bool m_brightnessSliderActive{};
	int m_brightnessOnOpen{};
	TickType_t m_brightnessSliderTimeout{};
	lv_timer_t* m_brightnessSliderTimer{};

	lv_obj_t* m_volumeSlider{};
	bool m_volumeSliderActive{};
	int m_volumeOnOpen{};
	TickType_t m_volumeSliderTimeout{};
	lv_timer_t* m_volumeSliderTimer{};

	lv_obj_t* m_batteryLabel{};
	lv_timer_t* m_batteryTimer{};

	// ── 尺寸常量 ──
	static constexpr int CARD_W = 240;
	static constexpr int CARD_H = 240;
	static constexpr uint8_t SliderGrow = 70;
	static constexpr uint8_t SpaceGrow = 30; // 空白区域
	static constexpr uint32_t SliderOpenTime = 200;
	static constexpr uint32_t SliderCloseTime = 100;

	// ── 私有方法 ──
	void buildUi();
	void updateSelectionLabels();
	void applyFocus();
	void activateFocus();
	lv_obj_t* statusTarget(int8_t idx);  // 状态栏焦点索引 → 目标 label（越界返回 nullptr）

	// 卡片导航
	void navCardsLeft();
	void navCardsRight();

	// 底部导航
	void navToBottom();
	void navFromBottomUp();

	// 状态栏导航
	void navToStatus();
	void navFromStatusDown();

	// 亮度滑块
	void showBrightnessSlider();
	void hideBrightnessSlider();
	static void onBrightnessSliderCb(lv_event_t* e);

	// 音量滑块
	void showVolumeSlider();
	void hideVolumeSlider();
	static void onVolumeSliderCb(lv_event_t* e);

	// 操作
	void startGame();
	void updateBatteryIcon();

	// ── 静态回调 ──
	static void onPrevBtnCb(lv_event_t* e);
	static void onNextBtnCb(lv_event_t* e);
	static void onStartBtnCb(lv_event_t* e);
	static void onBluetoothLabelCb(lv_event_t* e);
	static void onVolumeLabelCb(lv_event_t* e);
	static void onBrightnessLabelCb(lv_event_t* e);
	static void onBatteryLabelCb(lv_event_t* e);

};
