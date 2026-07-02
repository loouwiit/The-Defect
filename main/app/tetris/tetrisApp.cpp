#include "tetrisApp.hpp"
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
        m_renderers[i]->bindGameState(&m_gameStates[i]);
        m_renderers[i]->bindPlayer(&m_players[i]);
    }

    // 初始化共享出块队列 + 游戏状态
    m_sharedQueue.reset();

    for (int i = 0; i < PLAYER_COUNT; i++) {
        m_players[i].setQueue(&m_sharedQueue);
        m_players[i].init();
        m_players[i].exportState(m_gameStates[i]);
        // 填充初始预览
        for (int s = 0; s < 4; s++)
            m_gameStates[i].nextPieces[s] = m_players[i].peekPreview(s);
    }

    // 初始化网络层（Host 模式）
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
            else { p.netLeftReq++; }
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
//  游戏循环（Host-authoritative）
// ============================================================

void TetrisApp::gameLoopTask(void* param)
{
    auto app = static_cast<TetrisApp*>(param);
    app->gameLoop();
    vTaskDelete(nullptr);
}

void TetrisApp::gameLoop()
{
    TickType_t lastTick = xTaskGetTickCount();
    TickType_t snapTick = 0;

    while (running) {
        TickType_t now = xTaskGetTickCount();

        // ── 1. 消费网络数据 ──
        tetrisNetUpdate();

        // ── 2. 推进所有玩家的游戏逻辑 ──
        for (int i = 0; i < PLAYER_COUNT; i++) {
            if (!m_players[i].gameOver) {
                m_players[i].processInput();
                m_players[i].updateGame();  // 内部可能 lockPiece → spawnPiece
            }
        }

        // ── 3. 跨玩家攻击路由 ──
        for (int i = 0; i < PLAYER_COUNT; i++) {
            int atk = m_players[i].attackOut();
            if (atk > 0) {
                int target = (i + 1) % PLAYER_COUNT;
                m_players[target].addGarbage(atk);
                m_players[i].clearAttackOut();
            }
        }

        // ── 4. 导出 GameState + 渲染（含预览，基于每玩家游标）──
        for (int i = 0; i < PLAYER_COUNT; i++) {
            // 填充预览（基于该玩家游标位置，考虑 Hold 槽）
            bool prevChanged = false;
            for (int s = 0; s < 4; s++) {
                PieceType p = m_players[i].peekPreview(s);
                if (p != m_gameStates[i].nextPieces[s]) prevChanged = true;
                m_gameStates[i].nextPieces[s] = p;
            }

            DirtyFlags flags = m_players[i].consumeDirty();
            if (flags) {
                m_players[i].exportState(m_gameStates[i]);
            }

            if (flags || prevChanged || m_gameStates[i].gameOver != m_players[i].gameOver) {
                if (auto guard = display->lockGuard())
                    m_renderers[i]->syncState();
            }
        }

        // ── 5. 发送 snapshot 给远程客户端（20fps） ──
        int remoteFd = tetrisNetHostGetFirstClientFd();
        if (remoteFd >= 0 && now - snapTick >= pdMS_TO_TICKS(50)) {
            int target = tetrisNetHostGetClientTarget();
            if (target < 0 || target >= PLAYER_COUNT) target = 0;
            tetrisNetHostSendSnapshot(remoteFd, target, m_gameStates[target]);
            snapTick = now;
        }

        // ── 6. 保持 60fps ──
        TickType_t elapsed = now - lastTick;
        if (elapsed < pdMS_TO_TICKS(16)) {
            vTaskDelay(pdMS_TO_TICKS(16) - elapsed);
        }
        lastTick = now;
    }

    ESP_LOGI(TAG, "game loop exit");
}


