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
static constexpr int HALF_W       = SCREEN_W / 2;  // 640
static constexpr int BTN_SIZE     = 52;
static constexpr int BTN_GAP      = 6;

// 每个玩家棋盘区基准偏移
static constexpr int BOARD_X      = 8;
static constexpr int BOARD_Y      = 8;

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

    // 分隔线
    auto divider = lv_obj_create(screen);
    lv_obj_remove_style_all(divider);
    lv_obj_set_style_bg_color(divider, GUI::Color::SUBTLE, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_size(divider, 2, SCREEN_H);
    lv_obj_set_pos(divider, HALF_W - 1, 0);

    // 创建 2 个渲染器，分左右半屏
    for (int i = 0; i < PLAYER_COUNT; i++) {
        m_renderers[i] = new TetrisRenderer(display, screen);
        lv_coord_t x = (i == 0) ? 0 : HALF_W;
        m_renderers[i]->setArea(x, 0, HALF_W, SCREEN_H);
    }

    // 创建 2 套触屏按钮
    createTouchButtons();

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
    TickType_t renderTick = lastTick;

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

        // 发送快照给远程客户端（30fps，匹配渲染节奏）
        TickType_t now = xTaskGetTickCount();
        if (remoteFd >= 0 && now - snapTick >= pdMS_TO_TICKS(33)) {
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

        // 渲染 30fps
        if (now - renderTick >= pdMS_TO_TICKS(33)) {
            if (auto guard = app->display->lockGuard()) {
                app->render();
            }
            renderTick = now;
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
//  渲染 — 分别渲染 2 个玩家
// ============================================================

void TetrisApp::render()
{
    for (int i = 0; i < PLAYER_COUNT; i++) {
        auto* r = m_renderers[i];
        auto& p = m_players[i];
        if (!r) continue;

        if (p.gameOver) {
            // 只绘制一次 Game Over 文字（用标签检测是否已创建）
            // 简单起见：继续擦除/重绘但棋盘不变
            r->clearPiece(m_lastPiece[i]);
            r->clearGhost(m_lastGhost[i]);
            continue;
        }

        // 1. 擦除上一帧
        r->clearPiece(m_lastPiece[i]);
        r->clearGhost(m_lastGhost[i]);

        // 2. 同步棋盘
        r->syncBoard(p.board);

        // 3. 绘制 ghost + 活动块
        auto color = pieceToColor(p.currentPiece.type());
        r->drawGhost(p.ghostPiece, color);
        r->drawPiece(p.currentPiece, color);

        // 4. 更新 NEXT 预览
        PieceType preview[4];
        for (int s = 0; s < 4; s++)
            preview[s] = p.peekPreview(s);
        r->drawNext(preview);

        // 5. 更新信息栏
        r->drawInfo(p.scoring.combo(), p.garbageFlash());

        // 6. 保存位置
        m_lastPiece[i] = p.currentPiece;
        m_lastGhost[i] = p.ghostPiece;
    }
}

// ============================================================
//  触屏按钮 — 每个玩家半屏内一套
// ============================================================

static int btnPlayerOf(void* btn)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(btn))));
}

void TetrisApp::createTouchButtons()
{
    auto makeBtn = [&](const char* text, lv_coord_t x, lv_coord_t y,
                       int playerIndex) -> lv_obj_t* {
        auto btn = lv_btn_create(screen);
        lv_obj_set_size(btn, BTN_SIZE, BTN_SIZE);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_bg_color(btn, GUI::Color::CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(playerIndex)));

        auto label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, GUI::Color::TEXT, 0);

        lv_obj_add_event_cb(btn, onBtnPressed, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(btn, onBtnReleased, LV_EVENT_RELEASED, this);

        return btn;
    };

    for (int player = 0; player < PLAYER_COUNT; player++) {
        // 每个玩家的棋盘在各自半屏内居中
        int areaX  = (player == 0) ? 0 : HALF_W;
        int btnRowY = BOARD_Y + BOARD_VISIBLE_H * 32 + 12;

        int totalBtnW = 8 * BTN_SIZE + 7 * BTN_GAP;
        int btnStartX = areaX + (HALF_W - totalBtnW) / 2;

        int bx = btnStartX;
        m_btnLeft[player]  = makeBtn("<",   bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;
        m_btnRight[player] = makeBtn(">",   bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;
        m_btnSoft[player]  = makeBtn("v",   bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;
        m_btnHard[player]  = makeBtn("V",   bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;
        m_btnCW[player]    = makeBtn("CW",  bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;
        m_btnCCW[player]   = makeBtn("CCW", bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;
        m_btnHold[player]  = makeBtn("H",   bx,                               btnRowY, player); bx += BTN_SIZE + BTN_GAP;

    }
}

void TetrisApp::onBtnPressed(lv_event_t* e)
{
    auto app = static_cast<TetrisApp*>(lv_event_get_user_data(e));
    auto btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int player = btnPlayerOf(btn);

    auto& p = app->m_players[player];

    if (btn == app->m_btnLeft[player])   { p.keyLeft  = true; }
    if (btn == app->m_btnRight[player])  { p.keyRight = true; }
    if (btn == app->m_btnSoft[player])   { p.keySoft  = true; }
    if (btn == app->m_btnCW[player])     { p.keyCW    = true; }
    if (btn == app->m_btnCCW[player])    { p.keyCCW   = true; }
    if (btn == app->m_btnHard[player])   { p.keyHard  = true; }
    if (btn == app->m_btnHold[player])   { p.keyHold  = true; }
}

void TetrisApp::onBtnReleased(lv_event_t* e)
{
    auto app = static_cast<TetrisApp*>(lv_event_get_user_data(e));
    auto btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    int player = btnPlayerOf(btn);

    auto& p = app->m_players[player];

    if (btn == app->m_btnLeft[player])   { p.keyLeft  = false; p.dasTimer = 0; }
    if (btn == app->m_btnRight[player])  { p.keyRight = false; p.arrTimer = 0; }
    if (btn == app->m_btnSoft[player])   { p.keySoft  = false; }
}


