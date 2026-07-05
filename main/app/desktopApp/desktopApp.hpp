#pragma once

#include "app/app.hpp"
#include <cstdint>

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
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	// ── 游戏数据 ──
	static constexpr int GAME_COUNT = 5;
	static const char* GAME_NAMES[GAME_COUNT];
	static const char* GAME_DESCS[GAME_COUNT];
	static const char* GAME_ICONS[GAME_COUNT];

	// ── 焦点导航组 ──
	enum FocusGroup : int8_t {
		FOCUS_CARDS = 0,  // 游戏卡片行（左右切换卡片）
		FOCUS_BOTTOM = 1,  // 底部"开始游戏"按钮
		FOCUS_STATUS = 2,  // 顶栏状态图标
	};
	FocusGroup m_focusGroup{ FOCUS_CARDS };
	int8_t m_focusCardsIdx{ 0 };      // 0..GAME_COUNT-1
	int8_t m_focusStatusIdx{ 0 };     // 0=时间, 1=WiFi, 2=蓝牙, 3=音量, 4=亮度, 5=电池
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons{};

	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;
	static constexpr TickType_t ACTION_DELAY = 500;

	// ── UI 对象成员 ──
	lv_obj_t* m_cardsRow{};              // 卡片 flex 容器
	lv_obj_t* m_gameCards[GAME_COUNT]{}; // 5 张卡片

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
