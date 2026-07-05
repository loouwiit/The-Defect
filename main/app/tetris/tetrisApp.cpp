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

TetrisApp::TetrisApp(Display* display, int playerCount)
    : App(display), m_playerCount(playerCount)
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

    // 每玩家宽度由人数决定
    lv_coord_t playerW = SCREEN_W / m_playerCount;
    for (int i = 0; i < m_playerCount; i++) {
        m_renderers[i] = new TetrisRenderer(display, flexRow, playerW);
        m_renderers[i]->bindGameState(&m_gameStates[i]);
        m_renderers[i]->bindPlayer(&m_players[i]);
    }

    // 初始化共享出块队列 + 游戏状态
    m_sharedQueue.reset();

    for (int i = 0; i < m_playerCount; i++) {
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
        if (target < 0 || target >= m_playerCount) target = 0;
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

    // ── 全局 Game Over UI（初始隐藏） ──
    createGameOverUI();

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

    for (int i = 0; i < m_playerCount; i++) {
        delete m_renderers[i];
        m_renderers[i] = nullptr;
    }

    App::deinit();
}

// ============================================================
//  BLE 手柄输入
// ============================================================

void TetrisApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
    if (playerId >= m_playerCount) return;
    auto& p = m_players[playerId];

    constexpr uint8_t deadZone = 50;
    constexpr uint8_t center = 128;

    // ── 摇杆 → 方向及软降 ──
    bool lxLeft  = (state.lx < center - deadZone);
    bool lxRight = (state.lx > center + deadZone);
    bool lyUp    = (state.ly < center - deadZone);
    bool lyDown  = (state.ly > center + deadZone);

    // D-pad 补充／覆盖摇杆（dpad 0~7=方向, 15=松开）
    if (state.dpad < 8) {
        lxLeft  = (state.dpad == 6 || state.dpad == 5 || state.dpad == 7);
        lxRight = (state.dpad == 2 || state.dpad == 1 || state.dpad == 3);
        lyUp    = (state.dpad == 0 || state.dpad == 1 || state.dpad == 7);
        lyDown  = (state.dpad == 4 || state.dpad == 3 || state.dpad == 5);
    }

    p.keyLeft  = lxLeft;
    p.keyRight = lxRight;
    p.keySoft  = lyDown;

    // ── 按钮边沿检测（仅刚按下时触发） ──
    uint16_t newPress = state.buttons & ~m_prevButtons[playerId];
    m_prevButtons[playerId] = state.buttons;

    // A → 逆时针旋转
    if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A))
        p.keyCCW = true;

    // B → 顺时针旋转
    if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
        p.keyCW = true;

    // X → Hold
    if (newPress & static_cast<uint16_t>(GamepadButton::BTN_X))
        p.keyHold = true;

    // Y → 硬降
    if (newPress & static_cast<uint16_t>(GamepadButton::BTN_Y))
        p.keyHard = true;
}

// ============================================================
//  全局 Game Over UI
// ============================================================

void TetrisApp::createGameOverUI()
{
    auto guard = display->lockGuard();
    if (!guard) return;

    m_gameOverOverlay = lv_obj_create(screen);
    lv_obj_set_size(m_gameOverOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(m_gameOverOverlay, 0, 0);
    lv_obj_set_style_bg_color(m_gameOverOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(m_gameOverOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(m_gameOverOverlay, 0, 0);
    lv_obj_add_flag(m_gameOverOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_SCROLLABLE);

    auto title = lv_label_create(m_gameOverOverlay);
    lv_label_set_text(title, "GAME OVER");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -60);

    m_restartBtn = lv_button_create(m_gameOverOverlay);
    lv_obj_set_size(m_restartBtn, 200, 60);
    lv_obj_set_style_radius(m_restartBtn, 16, 0);
    lv_obj_set_style_bg_color(m_restartBtn, lv_color_hex(0x4A6CF7), 0);
    lv_obj_set_style_bg_opa(m_restartBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(m_restartBtn, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(m_restartBtn, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_align(m_restartBtn, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_event_cb(m_restartBtn, onRestartCb, LV_EVENT_CLICKED, this);
    auto lblR = lv_label_create(m_restartBtn);
    lv_label_set_text(lblR, "重新开始");
    lv_obj_center(lblR);

    m_backBtn = lv_button_create(m_gameOverOverlay);
    lv_obj_set_size(m_backBtn, 200, 60);
    lv_obj_set_style_radius(m_backBtn, 16, 0);
    lv_obj_set_style_bg_color(m_backBtn, lv_color_hex(0x555566), 0);
    lv_obj_set_style_bg_opa(m_backBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(m_backBtn, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
    lv_obj_align(m_backBtn, LV_ALIGN_CENTER, 0, 90);
    lv_obj_add_event_cb(m_backBtn, onBackToRoomCb, LV_EVENT_CLICKED, this);
    auto lblB = lv_label_create(m_backBtn);
    lv_label_set_text(lblB, "返回房间");
    lv_obj_center(lblB);
}

void TetrisApp::onRestartCb(lv_event_t* e)
{
    auto* self = static_cast<TetrisApp*>(lv_event_get_user_data(e));
    Task::addTask([](void* p) -> TickType_t {
        auto* app = static_cast<TetrisApp*>(p);
        app->replaceWith(new TetrisApp(app->display, app->m_playerCount));
        return Task::infinityTime;
    }, "tetrisRestart", self, 0, Task::Affinity::None);
}

void TetrisApp::onBackToRoomCb(lv_event_t* e)
{
    auto* self = static_cast<TetrisApp*>(lv_event_get_user_data(e));
    Task::addTask([](void* p) -> TickType_t {
        static_cast<TetrisApp*>(p)->popApp();
        return Task::infinityTime;
    }, "tetrisBackToRoom", self, 0, Task::Affinity::None);
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
        for (int i = 0; i < m_playerCount; i++) {
            if (!m_players[i].gameOver) {
                m_players[i].processInput();
                m_players[i].updateGame();  // 内部可能 lockPiece → spawnPiece
            }
        }

        // ── 3. 跨玩家攻击路由 ──
        for (int i = 0; i < m_playerCount; i++) {
            int atk = m_players[i].attackOut();
            if (atk > 0) {
                int target = (i + 1) % m_playerCount;
                m_players[target].addGarbage(atk);
                m_players[i].clearAttackOut();
            }
        }

        // ── 4. 导出 GameState + 渲染（含预览，基于每玩家游标）──
        for (int i = 0; i < m_playerCount; i++) {
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
            if (target < 0 || target >= m_playerCount) target = 0;
            tetrisNetHostSendSnapshot(remoteFd, target, m_gameStates[target]);
            snapTick = now;
        }

        // ── 6. 全局 Game Over 检测 ──
        bool allDead = true;
        for (int i = 0; i < m_playerCount; i++) {
            if (!m_players[i].gameOver) { allDead = false; break; }
        }
        if (allDead && !m_allDead) {
            m_allDead = true;
            if (auto guard = display->lockGuard())
                lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_HIDDEN);
        }

        // ── 7. 保持 60fps ──
        TickType_t elapsed = now - lastTick;
        if (elapsed < pdMS_TO_TICKS(16)) {
            vTaskDelay(pdMS_TO_TICKS(16) - elapsed);
        }
        lastTick = now;
    }

    ESP_LOGI(TAG, "game loop exit");
}


