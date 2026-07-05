#pragma once

#include "app/app.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"
#include "app/tetris/gameLogic/player_state.hpp"
#include "app/tetris/gameLogic/gameState.hpp"
#include "app/tetris/renderer/tetris_renderer.hpp"
#include "app/tetris/net/tetris_net.hpp"
#include "task/task.hpp"

/**
 * @brief 俄罗斯方块主游戏 App
 *
 * Host-authoritative 模式：
 *   - 所有 PlayState 游戏逻辑只运行在 Host 上
 *   - 共享 PieceQueue（唯一 next() 游标）
 *   - 每帧将 GameState 导出给 Renderer 和 Network
 *   - 远程客户端只转发输入 + 收 snapshot 渲染
 */
class TetrisApp : public App
{
public:
    constexpr static char TAG[] = "TetrisApp";

    TetrisApp(Display* display, int playerCount = 3);
    ~TetrisApp() override;

    void init() override;
    void deinit() override;

    // ── BLE 手柄输入 ──
    void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
    int m_playerCount;

    static constexpr int MAX_PLAYERS = 3;

    // ============================================================
    //  共享出块队列（所有玩家共用唯一 next() 游标）
    // ============================================================
    PieceQueue  m_sharedQueue;

    // ============================================================
    //  游戏逻辑（仅 Host 运行）
    // ============================================================
    PlayerState m_players[MAX_PLAYERS];

    // ============================================================
    //  导出状态（供 Renderer / Network 使用）
    // ============================================================
    GameState   m_gameStates[MAX_PLAYERS];

    // ============================================================
    //  渲染（每玩家独立）
    // ============================================================
    TetrisRenderer* m_renderers[MAX_PLAYERS] = {};

    // ============================================================
    //  游戏线程
    // ============================================================
    Thread* m_gameThread = nullptr;

    // ============================================================
    //  手柄输入状态
    // ============================================================
    uint16_t m_prevButtons[MAX_PLAYERS] = {};
    bool     m_lastLyUp[MAX_PLAYERS] = {};

    // ============================================================
    //  全局 Game Over
    // ============================================================
    lv_obj_t* m_gameOverOverlay{};
    lv_obj_t* m_restartBtn{};
    lv_obj_t* m_backBtn{};
    bool      m_allDead = false;
    int8_t    m_focusGameOverIdx = 0;
    TickType_t m_nextMoveTime = 0;
    uint16_t  m_prevBtnsAll = 0;

	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;
	static constexpr TickType_t ACTION_DELAY = 500;

    void createGameOverUI();
    static void onRestartCb(lv_event_t* e);
    static void onBackToRoomCb(lv_event_t* e);

    static void gameLoopTask(void* param);
    void gameLoop();
};
