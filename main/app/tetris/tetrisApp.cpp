#include "tetrisApp.hpp"
#include "app/tetris/net/tetris_net.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"
#include <esp_log.h>


// ============================================================
//  分屏 & 按钮尺寸
// ============================================================
static constexpr int SCREEN_W     = 1280;
static constexpr int SCREEN_H     = 720;

// ============================================================
//  构造 / 析构
// ============================================================

TetrisApp::TetrisApp(Display* display)
    : App(display)
{
}

TetrisApp::~TetrisApp()
{
    if (m_gameThread && m_gameThread->isRunning()) {
        deinit();
    }
}

// ============================================================
//  init / deinit
// ============================================================

void TetrisApp::init()
{
    App::init();

    auto guard = display->lockGuard();
    if (!guard) {
        ESP_LOGE(TAG, "Failed to lock display");
        return;
    }

    // 背景
    lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, FontLoader::getDefault(), 0);

    // Flex 行容器：玩家自动等宽排列
    auto flexRow = lv_obj_create(screen);
    lv_obj_remove_style_all(flexRow);
    lv_obj_set_size(flexRow, SCREEN_W, SCREEN_H);
    lv_obj_set_flex_flow(flexRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollbar_mode(flexRow, LV_SCROLLBAR_MODE_OFF);

    // 每玩家宽度由人数决定，flex_grow 自动分配剩余空间
    lv_coord_t playerW = SCREEN_W / PLAYER_COUNT;
    for (int i = 0; i < PLAYER_COUNT; i++) {
        m_renderers[i] = new TetrisRenderer(display, flexRow, playerW);
        m_renderers[i]->bindPlayer(&m_players[i]);
    }

    // 初始化共享出块队列
    m_sharedQueue.reset();

    // 初始化游戏状态（所有玩家共享同一出块序列）
    for (int i = 0; i < PLAYER_COUNT; i++) {
        m_players[i].setQueue(&m_sharedQueue);
        m_players[i].init();
    }

    // 初始化网络层（注册 /ws/tetris handler，Host 模式）
    TetrisHostCallbacks hostCb;
    hostCb.onJoin = [](int playerId, int fd) {
        ESP_LOGI(TAG, "remote player %d joined (fd=%d)", playerId, fd);
    };
    hostCb.onInput = [this](int playerId, const char* key, bool down) {
        int target = playerId;
        if (target < 0 || target >= PLAYER_COUNT) target = 0;
        auto& p = m_players[target];
        if (strcmp(key, "left") == 0) {
            p.keyLeft = down;
            if (!down) { p.dasTimer = 0; }
            else { p.netLeftReq++; }  // 网络防吞计数
        }
        else if (strcmp(key, "right") == 0) {
            p.keyRight = down;
            if (!down) { p.arrTimer = 0; }
            else { p.netRightReq++; }
        }
        else if (strcmp(key, "soft") == 0)  { p.keySoft  = down; }
        else if (strcmp(key, "hard") == 0)  { if(down) p.keyHard = true; }
        else if (strcmp(key, "cw") == 0)    { if(down) p.keyCW   = true; }
        else if (strcmp(key, "ccw") == 0)   { if(down) p.keyCCW  = true; }
        else if (strcmp(key, "hold") == 0)  { if(down) p.keyHold = true; }
    };
    hostCb.onAttack = [this](int playerId, int lines) {
        ESP_LOGI(TAG, "attack from player %d: %d lines → local player 0", playerId, lines);
        m_players[0].addGarbage(lines);
    };
    hostCb.onGameOver = [](int playerId) {
        ESP_LOGI(TAG, "player %d game over", playerId);
    };
    tetrisNetInit(true, hostCb, {});

    // 启动游戏线程
    m_gameThread = new Thread(gameLoopTask, "tetrisGame", this,
                              Thread::Priority::Normal, 4096);
}

void TetrisApp::deinit()
{
    tetrisNetDeinit();

    if (m_gameThread) {
        if (m_gameThread->isRunning()) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        delete m_gameThread;
        m_gameThread = nullptr;
    }

    for (int i = 0; i < PLAYER_COUNT; i++) {
        delete m_renderers[i];
        m_renderers[i] = nullptr;
    }

    App::deinit();
}

// ============================================================
//  游戏线程 — 每帧更新 2 个玩家
// ============================================================

void TetrisApp::gameLoopTask(void* param)
{
    auto app = static_cast<TetrisApp*>(param);

    TickType_t lastTick = xTaskGetTickCount();

    int remoteFd = -1;
    TickType_t snapTick = 0;  // 快照计时器

    while (app->running) {
        // 更新 2 个玩家
        for (int i = 0; i < PLAYER_COUNT; i++) {
            app->m_players[i].processInput();
            app->m_players[i].updateGame();
        }

        // 跨玩家应用攻击（消行 → 垃圾行）
        for (int i = 0; i < PLAYER_COUNT; i++) {
            int atk = app->m_players[i].attackOut();
            if (atk > 0) {
                int target = (i + 1) % PLAYER_COUNT;
                app->m_players[target].addGarbage(atk);
                if (remoteFd >= 0) tetrisNetHostSendGarbage(remoteFd, atk);
                app->m_players[i].clearAttackOut();
            }
        }

        // 刷新远程客户端 fd
        remoteFd = tetrisNetHostGetFirstClientFd();

        // 事件驱动渲染 — 仅处理有 dirty flag 的玩家
        for (int i = 0; i < PLAYER_COUNT; i++) {
            DirtyFlags flags = app->m_players[i].consumeDirty();
            if (flags) {
                if (auto guard = app->display->lockGuard())
                    app->m_renderers[i]->syncDirty(app->m_players[i], flags);
            }
        }

        // 发送快照给远程客户端（10fps）
        TickType_t now = xTaskGetTickCount();
        if (remoteFd >= 0 && now - snapTick >= pdMS_TO_TICKS(100)) {
            int target = tetrisNetHostGetClientTarget();
            if (target < 0 || target >= PLAYER_COUNT) target = 0;
            auto& p = app->m_players[target];
            PieceType preview[4];
            for (int s = 0; s < 4; s++) preview[s] = p.peekPreview(s);
            int gy = calculateGhost(p.currentPiece, p.board).y();
            tetrisNetHostSendBoardSnapshot(remoteFd, 0,
                p.board, p.currentPiece, gy,
                p.scoring.score(), p.scoring.level(), p.scoring.totalLines(),
                preview, 4, p.holdPiece(), p.pendingGarbage());
            snapTick = now;
        }

        // 游戏逻辑 60fps
        TickType_t elapsed = now - lastTick;
        if (elapsed < pdMS_TO_TICKS(16)) {
            vTaskDelay(pdMS_TO_TICKS(16) - elapsed);
        }
        lastTick = now;
    }

    ESP_LOGI(TAG, "game thread exit");
    vTaskDelete(nullptr);
}

// ============================================================
//  触屏按钮 — 每个玩家半屏内一套
// ============================================================

// 按钮创建和回调已移至 TetrisRenderer


