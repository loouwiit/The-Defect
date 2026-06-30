#pragma once

#include "app/app.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"
#include "app/tetris/gameLogic/player_state.hpp"
#include "app/tetris/renderer/tetris_renderer.hpp"
#include "task/task.hpp"

/**
 * @brief 俄罗斯方块主游戏 App
 *
 * 单机模式：完整的游戏循环 + 渲染 + 触屏虚拟按钮。
 * 内部使用 PlayerState 管理游戏逻辑。
 *
 * 双人模式：持有 2 个 PlayerState，各自独立运行游戏循环。
 */
class TetrisApp : public App
{
public:
    constexpr static char TAG[] = "TetrisApp";

    TetrisApp(Display* display);
    ~TetrisApp() override;

    void init() override;
    void deinit() override;

private:
    static constexpr int PLAYER_COUNT = 3;

    // ============================================================
    //  共享出块队列 + 游戏状态（每个玩家独立）
    // ============================================================
    PieceQueue      m_sharedQueue;
    PlayerState     m_players[PLAYER_COUNT];

    // ============================================================
    //  渲染（每个玩家独立）
    // ============================================================
    TetrisRenderer* m_renderers[PLAYER_COUNT] = {};

    // ============================================================
    //  游戏线程
    // ============================================================
    Thread* m_gameThread = nullptr;

    // ============================================================
    //  内部方法
    // ============================================================

    static void gameLoopTask(void* param);
};
