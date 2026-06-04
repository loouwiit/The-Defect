#pragma once

#include "app/app.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"
#include "app/tetris/renderer/tetris_renderer.hpp"
#include "task/task.hpp"

/**
 * @brief 俄罗斯方块主游戏 App
 *
 * 单机模式：完整的游戏循环 + 渲染 + 触屏虚拟按钮。
 * 用于验证 Phase 1 游戏引擎的正确性。
 */
class TetrisApp : public App
{
public:
    TetrisApp(Display* display);
    ~TetrisApp() override;

    void init() override;
    void deinit() override;

private:
    // ============================================================
    //  游戏状态
    // ============================================================
    Board       m_board;
    PieceQueue  m_queue;
    Scoring     m_scoring;

    Piece       m_currentPiece;
    Piece       m_ghostPiece;
    Piece       m_lastPiece;      // 上一帧活动块位置（供擦除用）
    Piece       m_lastGhost;      // 上一帧 Ghost 位置（供擦除用）
    PieceType   m_holdPiece = PieceType::NONE;
    bool        m_holdUsed = false;    // 当前方块是否已使用 Hold
    bool        m_gameOver = false;

    // ============================================================
    //  计时器状态 (FreeRTOS tick, 1 tick = 1ms)
    // ============================================================
    TickType_t  m_gravityTimer = 0;
    TickType_t  m_lockTimer    = 0;
    TickType_t  m_dasTimer     = 0;
    TickType_t  m_arrTimer     = 0;
    int         m_gravityInterval = 1000;  // ms, 初始 1秒/行

    // 输入状态
    bool m_keyLeft    = false;
    bool m_keyRight   = false;
    bool m_keyCW      = false;
    bool m_keyCCW     = false;
    bool m_keySoft    = false;
    bool m_keyHard    = false;
    bool m_keyHold    = false;
    bool m_keyPause   = false;

    // ============================================================
    //  渲染
    // ============================================================
    TetrisRenderer* m_renderer = nullptr;

    // ============================================================
    //  游戏线程
    // ============================================================
    Thread* m_gameThread = nullptr;

    // ============================================================
    //  UI 对象 (触屏按钮)
    // ============================================================
    lv_obj_t* m_btnLeft   = nullptr;
    lv_obj_t* m_btnRight  = nullptr;
    lv_obj_t* m_btnCW     = nullptr;
    lv_obj_t* m_btnCCW    = nullptr;
    lv_obj_t* m_btnSoft   = nullptr;
    lv_obj_t* m_btnHard   = nullptr;
    lv_obj_t* m_btnHold   = nullptr;
    lv_obj_t* m_btnPause  = nullptr;

    // ============================================================
    //  内部方法
    // ============================================================

    // 游戏循环入口 (Thread 回调)
    static void gameLoopTask(void* param);

    // 单帧更新
    void processInput();
    void updateGame();
    void render();

    // 方块操作
    bool spawnPiece();
    bool movePiece(int dx, int dy);
    bool rotatePieceCW();
    bool rotatePieceCCW();
    void hardDrop();
    void lockPiece();
    void holdPiece();
    void addGarbage(int lines);

    // 重力速度计算
    int calcGravityInterval() const;

    // UI 创建
    void createTouchButtons();
    static void onBtnPressed(lv_event_t* e);
    static void onBtnReleased(lv_event_t* e);
};
