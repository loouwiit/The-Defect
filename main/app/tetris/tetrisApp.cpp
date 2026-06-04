#include "tetrisApp.hpp"
#include "display/font.hpp"
#include "app/desktopApp/gui.hpp"
#include <esp_log.h>

static constexpr char TAG[] = "TetrisApp";

// ============================================================
//  按钮尺寸
// ============================================================
static constexpr int BTN_SIZE = 56;
static constexpr int BTN_GAP  = 8;

// ============================================================
//  构造 / 析构
// ============================================================

TetrisApp::TetrisApp(Display* display)
    : App(display)
{
}

TetrisApp::~TetrisApp()
{
    // 确保线程已停止
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

    // 设置背景
    lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // 创建渲染器
    m_renderer = new TetrisRenderer(display, screen);

    // 创建触屏按钮
    createTouchButtons();

    // 加载字体到 screen
    lv_obj_set_style_text_font(screen, FontLoader::getDefault(), 0);

    // 初始化游戏
    m_board.clear();
    m_scoring.reset();
    m_queue.reset();
    m_holdPiece = PieceType::NONE;
    m_holdUsed = false;
    m_gameOver = false;
    m_gravityInterval = calcGravityInterval();

    // 生成第一个方块
    spawnPiece();

    // 启动游戏线程
    m_gameThread = new Thread(gameLoopTask, "tetrisGame", this,
                              Thread::Priority::Normal, 4096);
}

void TetrisApp::deinit()
{
    // 停止游戏线程
    if (m_gameThread) {
        if (m_gameThread->isRunning()) {
            // 线程会在下一次循环检测到 running=false 后退出
            // 但我们需要等待它
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        delete m_gameThread;
        m_gameThread = nullptr;
    }

    delete m_renderer;
    m_renderer = nullptr;

    App::deinit();
}

// ============================================================
//  游戏线程
// ============================================================

void TetrisApp::gameLoopTask(void* param)
{
    auto app = static_cast<TetrisApp*>(param);

    TickType_t lastTick = xTaskGetTickCount();
    TickType_t renderTick = lastTick;

    while (app->running) {
        app->processInput();
        app->updateGame();

        // 渲染 30fps (33ms)，减少 LVGL 锁竞争
        TickType_t now = xTaskGetTickCount();
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
//  输入处理
// ============================================================

void TetrisApp::processInput()
{
    if (m_gameOver) return;

    TickType_t now = xTaskGetTickCount();

    // --- 左右移动 (DAS/ARR) ---
    auto handleDirection = [&](bool key, int dx, TickType_t& das, TickType_t& arr) {
        if (key) {
            if (das == 0) {
                // 首次按下
                movePiece(dx, 0);
                das = now;
                arr = now;
            } else {
                TickType_t elapsed = now - das;
                if (elapsed >= pdMS_TO_TICKS(DAS_DELAY_MS)) {
                    TickType_t arrElapsed = now - arr;
                    if (arrElapsed >= pdMS_TO_TICKS(ARR_RATE_MS)) {
                        movePiece(dx, 0);
                        arr = now;
                    }
                }
            }
        } else {
            das = 0;
            arr = 0;
        }
    };

    handleDirection(m_keyLeft,  -1, m_dasTimer, m_arrTimer);
    handleDirection(m_keyRight, +1, m_arrTimer, m_dasTimer);  // 共用计时器简化

    // --- 旋转 ---
    if (m_keyCW) {
        rotatePieceCW();
        m_keyCW = false;
    }
    if (m_keyCCW) {
        rotatePieceCCW();
        m_keyCCW = false;
    }

    // --- 硬降 ---
    if (m_keyHard) {
        hardDrop();
        m_keyHard = false;
    }

    // --- Hold ---
    if (m_keyHold) {
        holdPiece();
        m_keyHold = false;
    }
}

// ============================================================
//  游戏更新
// ============================================================

void TetrisApp::updateGame()
{
    if (m_gameOver) return;

    TickType_t now = xTaskGetTickCount();

    // 软降
    if (m_keySoft) {
        if (now - m_gravityTimer >= pdMS_TO_TICKS(SOFT_DROP_MS)) {
            if (!movePiece(0, -1)) {
                // 不能下移 → 锁定
                lockPiece();
                return;
            }
            m_scoring.processLines(0, false, false, 1, 0);
            m_gravityTimer = now;
        }
    } else {
        // 重力下落
        if (now - m_gravityTimer >= pdMS_TO_TICKS(m_gravityInterval)) {
            if (!movePiece(0, -1)) {
                lockPiece();
                return;
            }
            m_gravityTimer = now;
        }
    }

    // Lock Delay (检测是否触底)
    int cols[4], yCoords[4];
    m_currentPiece.getBlocks(cols, yCoords);
    bool onGround = false;
    for (int i = 0; i < 4; i++) {
        if (m_board.collides(m_currentPiece.moved(0, -1))) {
            onGround = true;
            break;
        }
    }

    if (onGround) {
        if (m_lockTimer == 0)
            m_lockTimer = now;
        else if (now - m_lockTimer >= pdMS_TO_TICKS(LOCK_DELAY_MS))
            lockPiece();
    } else {
        m_lockTimer = 0;
    }

    // 更新 Ghost
    m_ghostPiece = calculateGhost(m_currentPiece, m_board);
}

// ============================================================
//  渲染
// ============================================================

void TetrisApp::render()
{
    if (!m_renderer) return;

    if (m_gameOver) {
        return;
    }

    // 1. 擦除上一帧的 ghost/piece（从 visual cache 恢复棋盘底色）
    m_renderer->clearPiece(m_lastPiece);
    m_renderer->clearGhost(m_lastGhost);

    // 2. 同步棋盘变化（diff，大部分帧无操作）
    m_renderer->syncBoard(m_board);

    // 3. 绘制当前 ghost 和活动块
    auto color = pieceToColor(m_currentPiece.type());
    m_renderer->drawGhost(m_ghostPiece, color);
    m_renderer->drawPiece(m_currentPiece, color);

    // 4. 更新侧栏
    m_renderer->drawHold(m_holdPiece, false);
    m_renderer->drawNext(m_queue);

    // 5. 保存当前位置，供下一帧擦除
    m_lastPiece = m_currentPiece;
    m_lastGhost = m_ghostPiece;
}

// ============================================================
//  方块操作
// ============================================================

bool TetrisApp::spawnPiece()
{
    PieceType type = m_queue.next();

    // I piece 居中
    int spawnX = (type == PieceType::O) ? 4 : 3;
    int spawnY = BOARD_HEIGHT - 2;  // 隐藏区顶部

    m_currentPiece = Piece(type, Rotation::R0, spawnX, spawnY);

    if (m_board.collides(m_currentPiece)) {
        m_gameOver = true;
        ESP_LOGI(TAG, "Game Over");
        return false;
    }

    m_holdUsed = false;
    m_gravityTimer = xTaskGetTickCount();
    m_lockTimer = 0;
    m_ghostPiece = calculateGhost(m_currentPiece, m_board);

    return true;
}

bool TetrisApp::movePiece(int dx, int dy)
{
    Piece moved = m_currentPiece.moved(dx, dy);
    if (!m_board.collides(moved)) {
        m_currentPiece = moved;
        // 移动重置 Lock Delay
        if (dy == -1)  // 下移重置 (触底)
            m_lockTimer = 0;
        return true;
    }
    return false;
}

bool TetrisApp::rotatePieceCW()
{
    Piece rotated = m_currentPiece.rotatedCW();

    // 尝试踢墙
    auto* kicks = (m_currentPiece.type() == PieceType::I) ? I_KICKS : JLSTZ_KICKS;
    int from = static_cast<int>(m_currentPiece.rotation());
    int to   = static_cast<int>(rotated.rotation());

    for (int test = 0; test < 5; test++) {
        KickOffset kick = kicks[from][to][test];
        Piece kicked = rotated.moved(kick.dx, kick.dy);
        if (!m_board.collides(kicked)) {
            m_currentPiece = kicked;
            m_lockTimer = 0;  // 旋转重置 Lock Delay
            return true;
        }
    }
    return false;
}

bool TetrisApp::rotatePieceCCW()
{
    Piece rotated = m_currentPiece.rotatedCCW();

    auto* kicks = (m_currentPiece.type() == PieceType::I) ? I_KICKS : JLSTZ_KICKS;
    int from = static_cast<int>(m_currentPiece.rotation());
    int to   = static_cast<int>(rotated.rotation());

    for (int test = 0; test < 5; test++) {
        KickOffset kick = kicks[from][to][test];
        Piece kicked = rotated.moved(kick.dx, kick.dy);
        if (!m_board.collides(kicked)) {
            m_currentPiece = kicked;
            m_lockTimer = 0;
            return true;
        }
    }
    return false;
}

void TetrisApp::hardDrop()
{
    int dropDistance = 0;
    Piece dropped = m_currentPiece;
    while (!m_board.collides(dropped.moved(0, -1))) {
        dropped = dropped.moved(0, -1);
        dropDistance++;
    }

    m_currentPiece = dropped;
    m_scoring.processLines(0, false, false, 0, dropDistance);
    lockPiece();
}

void TetrisApp::lockPiece()
{
    // 放置到棋盘
    m_board.place(m_currentPiece, pieceToColor(m_currentPiece.type()));

    // 检测消行
    int clearedY[4];
    int lines = m_board.clearLines(clearedY);

    // 计算攻击（单机暂不使用）
    bool isTSpin = false;  // TODO: T-Spin 检测
    bool isTSpinMini = false;
    int earned = m_scoring.processLines(lines, isTSpin, isTSpinMini, 0, 0);

    ESP_LOGI(TAG, "lock: lines=%d score=%d total=%d",
             lines, earned, m_scoring.score());

    // 更新重力速度
    m_gravityInterval = calcGravityInterval();

    // 生成下一个方块
    if (!spawnPiece()) {
        // Game Over
        if (auto guard = display->lockGuard()) {
            lv_obj_t* goLabel = lv_label_create(screen);
            lv_label_set_text(goLabel, "GAME OVER");
            lv_obj_set_style_text_color(goLabel, GUI::Color::DANGER, 0);
            lv_obj_set_style_text_font(goLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
            lv_obj_center(goLabel);
        }
    }
}

void TetrisApp::holdPiece()
{
    if (m_holdUsed) return;  // 每块只能 Hold 一次

    if (m_holdPiece == PieceType::NONE) {
        // 首次 Hold：保存当前块，从队列取新块
        m_holdPiece = m_currentPiece.type();
        spawnPiece();
    } else {
        // 交换
        PieceType currentType = m_currentPiece.type();
        int spawnX = (m_holdPiece == PieceType::O) ? 4 : 3;
        int spawnY = BOARD_HEIGHT - 2;
        m_currentPiece = Piece(m_holdPiece, Rotation::R0, spawnX, spawnY);
        m_holdPiece = currentType;

        if (m_board.collides(m_currentPiece)) {
            m_gameOver = true;
            return;
        }

        m_gravityTimer = xTaskGetTickCount();
        m_lockTimer = 0;
        m_ghostPiece = calculateGhost(m_currentPiece, m_board);
    }

    m_holdUsed = true;
}

void TetrisApp::addGarbage(int lines)
{
    int colHole = rand() % BOARD_WIDTH;
    m_board.addGarbage(lines, colHole);
}

// ============================================================
//  重力速度计算
// ============================================================

int TetrisApp::calcGravityInterval() const
{
    int level = m_scoring.level();
    // Guideline 速度曲线（简化）
    // Level 0: 1000ms, Level 1: 800ms, ... Level 9: 100ms, 之后 50ms
    if (level >= 9) return 50;
    return 1000 - level * 100;
}

// ============================================================
//  触屏按钮
// ============================================================

void TetrisApp::createTouchButtons()
{
    auto makeBtn = [&](const char* text, lv_coord_t x, lv_coord_t y) -> lv_obj_t* {
        auto btn = lv_btn_create(screen);
        lv_obj_set_size(btn, BTN_SIZE, BTN_SIZE);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, GUI::Color::CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        auto label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, GUI::Color::TEXT, 0);

        // 按下/释放事件
        lv_obj_add_event_cb(btn, onBtnPressed, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(btn, onBtnReleased, LV_EVENT_RELEASED, this);

        return btn;
    };

    // 触屏按钮 — 在棋盘下方，单行居中
    int boardW = BOARD_WIDTH * m_renderer->cellSize();  // 320
    int boardX = 16;
    int boardY = 16;
    int btnRowY = boardY + BOARD_VISIBLE_H * m_renderer->cellSize() + 12;  // board bottom + gap

    int totalBtnW = 8 * BTN_SIZE + 7 * BTN_GAP;
    int btnStartX = boardX + (boardW - totalBtnW) / 2;
    if (btnStartX < boardX) btnStartX = boardX;

    int bx = btnStartX;
    m_btnLeft  = makeBtn("<",  bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnRight = makeBtn(">",  bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnSoft  = makeBtn("v",  bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnHard  = makeBtn("V",  bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnCW    = makeBtn("CW", bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnCCW   = makeBtn("CCW", bx,                              btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnHold  = makeBtn("H",  bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
    m_btnPause = makeBtn("||", bx,                               btnRowY); bx += BTN_SIZE + BTN_GAP;
}

void TetrisApp::onBtnPressed(lv_event_t* e)
{
    auto app = static_cast<TetrisApp*>(lv_event_get_user_data(e));
    auto btn = lv_event_get_target(e);

    if (btn == app->m_btnLeft)   { app->m_keyLeft  = true; }
    if (btn == app->m_btnRight)  { app->m_keyRight = true; }
    if (btn == app->m_btnSoft)   { app->m_keySoft  = true; }
    if (btn == app->m_btnCW)     { app->m_keyCW    = true; }
    if (btn == app->m_btnCCW)    { app->m_keyCCW   = true; }
    if (btn == app->m_btnHard)   { app->m_keyHard  = true; }
    if (btn == app->m_btnHold)   { app->m_keyHold  = true; }
}

void TetrisApp::onBtnReleased(lv_event_t* e)
{
    auto app = static_cast<TetrisApp*>(lv_event_get_user_data(e));
    auto btn = lv_event_get_target(e);

    if (btn == app->m_btnLeft)   { app->m_keyLeft  = false; app->m_dasTimer = 0; }
    if (btn == app->m_btnRight)  { app->m_keyRight = false; app->m_arrTimer = 0; }
    if (btn == app->m_btnSoft)   { app->m_keySoft  = false; }
}
