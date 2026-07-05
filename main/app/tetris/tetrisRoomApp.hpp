#pragma once

#include "app/app.hpp"
#include "bleGamepad/gamepadState.hpp"

/**
 * @brief 俄罗斯方块房间 / 选人界面
 *
 * 功能：
 *   - 显示玩家槽位及手柄连接状态
 *   - 选择玩家人数 (1P/2P/3P)
 *   - 跳转 BleSettingsApp 配对手柄
 *   - 启动 TetrisApp
 */
class TetrisRoomApp final : public App
{
public:
    static constexpr char TAG[] = "TetrisRoomApp";

    TetrisRoomApp(Display* display);
    ~TetrisRoomApp() override;

    void init() override;
    void onForeground() override;

    // BLE 手柄输入
    void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
    static constexpr int MAX_PLAYERS = 3;

    // ── UI ──
    lv_obj_t* m_backBtn{};
    lv_obj_t* m_btn1P{};
    lv_obj_t* m_btn2P{};
    lv_obj_t* m_btn3P{};
    lv_obj_t* m_btnSettings{};
    lv_obj_t* m_btnStart{};

    // 玩家状态卡片
    lv_obj_t* m_slotCards[MAX_PLAYERS]{};
    lv_obj_t* m_slotNames[MAX_PLAYERS]{};
    lv_obj_t* m_slotStatus[MAX_PLAYERS]{};
    lv_obj_t* m_slotActivity[MAX_PLAYERS]{};   // 活动指示点

    // 活动检测
    TickType_t m_lastActivity[MAX_PLAYERS]{};
    bool       m_lastActivityState[MAX_PLAYERS]{};
    static constexpr TickType_t ACTIVITY_TIMEOUT = pdMS_TO_TICKS(100);

    int8_t m_selectedPlayers = 2;  // 默认 2 人

    // 焦点导航
    static constexpr int FOCUS_COUNT = 6;  // back, 1P, 2P, 3P, settings, start
    int8_t m_focusIdx = 1;  // 默认聚焦 2P
    TickType_t m_nextMoveTime = 0;
    uint16_t m_prevButtons = 0;

    static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
    static constexpr TickType_t MOVE_DELAY       = pdMS_TO_TICKS(150);

    void buildUi();
    void applyFocus();
    void activateFocus();
    void refreshSlotStatus();
    void updateActivity();

    /** @brief 启动游戏 */
    void startGame();

    static void onBackBtnCb(lv_event_t* e);
    static void onPlayerBtnCb(lv_event_t* e);
    static void onSettingsBtnCb(lv_event_t* e);
    static void onStartBtnCb(lv_event_t* e);
};
