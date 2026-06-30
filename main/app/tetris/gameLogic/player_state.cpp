#include "player_state.hpp"
#include <esp_log.h>

static constexpr char TAG[] = "PlayerState";

// ============================================================
//  init / reset
// ============================================================

void PlayerState::init()
{
    reset();

    // 生成第一个方块
    spawnPiece();
}

void PlayerState::reset()
{
    board.clear();
    scoring.reset();
    holdUsed = false;
    gameOver = false;
    m_pieceIndex = 0;
    m_holdSlot = PieceType::NONE;
    m_pendingGarbage = 0;
    m_garbageFlash = 0;
    gravityInterval = calcGravityInterval();

    gravityTimer = 0;
    lockTimer = 0;
    dasTimer = 0;
    arrTimer = 0;

    keyLeft = keyRight = keyCW = keyCCW = keySoft = keyHard = keyHold = false;
    netLeftReq = netRightReq = 0;
}

// ============================================================
//  方块操作
// ============================================================

void PlayerState::spawnType(PieceType type)
{
    int spawnX = (type == PieceType::O) ? 4 : 3;
    int spawnY = BOARD_HEIGHT - 2;  // 隐藏区顶部

    currentPiece = Piece(type, Rotation::R0, spawnX, spawnY);

    if (board.collides(currentPiece)) {
        gameOver = true;
        ESP_LOGI(TAG, "Game Over");
        return;
    }

    holdUsed = false;
    m_garbageFlash = 0;  // 新块生成，清除垃圾提示
    gravityTimer = xTaskGetTickCount();
    lockTimer = 0;
    ghostPiece = calculateGhost(currentPiece, board);
}

bool PlayerState::spawnPiece()
{
    spawnType(nextPiece());
    return !gameOver;
}

bool PlayerState::movePiece(int dx, int dy)
{
    Piece moved = currentPiece.moved(dx, dy);
    if (!board.collides(moved)) {
        currentPiece = moved;
        // 水平移动取消旋转进入标记（T-Spin 判定用）
        if (dx != 0)
            m_lastActionRotated = false;
        // 下移重置 Lock Delay
        if (dy == -1)
            lockTimer = 0;
        return true;
    }
    return false;
}

bool PlayerState::rotateCW()
{
    Piece rotated = currentPiece.rotatedCW();

    auto* kicks = (currentPiece.type() == PieceType::I) ? I_KICKS : JLSTZ_KICKS;
    int from = static_cast<int>(currentPiece.rotation());
    int to   = static_cast<int>(rotated.rotation());

    for (int test = 0; test < 5; test++) {
        KickOffset kick = kicks[from][to][test];
        Piece kicked = rotated.moved(kick.dx, kick.dy);
        if (!board.collides(kicked)) {
            currentPiece = kicked;
            m_lastActionRotated = true;  // 旋转进入，T-Spin 候选
            lockTimer = 0;
            return true;
        }
    }
    return false;
}

bool PlayerState::rotateCCW()
{
    Piece rotated = currentPiece.rotatedCCW();

    auto* kicks = (currentPiece.type() == PieceType::I) ? I_KICKS : JLSTZ_KICKS;
    int from = static_cast<int>(currentPiece.rotation());
    int to   = static_cast<int>(rotated.rotation());

    for (int test = 0; test < 5; test++) {
        KickOffset kick = kicks[from][to][test];
        Piece kicked = rotated.moved(kick.dx, kick.dy);
        if (!board.collides(kicked)) {
            currentPiece = kicked;
            m_lastActionRotated = true;  // 旋转进入，T-Spin 候选
            lockTimer = 0;
            return true;
        }
    }
    return false;
}

void PlayerState::hardDrop()
{
    int dropDistance = 0;
    Piece dropped = currentPiece;
    while (!board.collides(dropped.moved(0, -1))) {
        dropped = dropped.moved(0, -1);
        dropDistance++;
    }

    currentPiece = dropped;
    scoring.processLines(0, false, false, 0, dropDistance);
    lockPiece();
}

void PlayerState::lockPiece()
{
    // 三角判定 + 旋转进入 → T-Spin
    // 只有 T 块旋转卡入 T 槽才算，横移蹭进去不算
    bool isTSpin = (currentPiece.type() == PieceType::T)
                   && m_lastActionRotated
                   && checkThreeCorner(currentPiece, board);

    // 放置到棋盘
    board.place(currentPiece, pieceToColor(currentPiece.type()));

    // 检测消行
    int clearedY[4];
    int lines = board.clearLines(clearedY);

    // 先保存 B2B 状态（scoring.processLines 内部会更新它）
    bool prevB2B = scoring.isB2B();

    // 计算得分
    scoring.processLines(lines, isTSpin, false, 0, 0);

    // 计算本次消行的攻击力
    int attackFromClear = calcAttackLines(lines, isTSpin, false, prevB2B, scoring.combo());

    // --- 垃圾行抵消 ---
    // 消行产生的攻击可以抵消待处理的垃圾行
    // 抵消后仍有剩余垃圾 → 注入棋盘
    // 攻击超出待处理垃圾 → 超出部分发给对手
    int garbageToApply = m_pendingGarbage - attackFromClear;
    if (garbageToApply > 0) {
        // 仍有垃圾未抵消，注入棋盘
        int colHole = static_cast<int>(xTaskGetTickCount() % BOARD_WIDTH);
        board.addGarbage(garbageToApply, colHole);
        ESP_LOGI(TAG, "garbage applied: %d", garbageToApply);
    }

    // 发送给对手的攻击 = 攻击力 - 被抵消的部分
    m_attackOut = (garbageToApply < 0) ? -garbageToApply : 0;
    m_pendingGarbage = 0;

    if (lines > 0)
        ESP_LOGI(TAG, "lock: lines=%d attack=%d score=%d",
                 lines, m_attackOut, scoring.score());
    else if (garbageToApply > 0)
        ESP_LOGI(TAG, "lock: no clear, garbage=%d", garbageToApply);

    // 更新重力速度
    gravityInterval = calcGravityInterval();

    // 生成下一个方块
    spawnPiece();
}

void PlayerState::doHold()
{
    if (holdUsed) return;  // 每块只能 Hold 一次

    PieceType currentType = currentPiece.type();

    if (m_holdSlot == PieceType::NONE) {
        // 首次 Hold：存进 holdSlot，直接从队列取下一块（不走 nextPiece）
        m_holdSlot = currentType;
        spawnType(m_queue->peek(m_pieceIndex++));
    } else {
        // 交换当前块 ↔ holdSlot
        PieceType slotType = m_holdSlot;
        m_holdSlot = currentType;
        spawnType(slotType);
    }

    holdUsed = true;
}

void PlayerState::addGarbage(int lines)
{
    m_pendingGarbage += lines;
    m_garbageFlash = lines;
    ESP_LOGI(TAG, "garbage queued: %d (total pending: %d)", lines, m_pendingGarbage);
}

// ============================================================
//  重力速度
// ============================================================

int PlayerState::calcGravityInterval() const
{
    int level = scoring.level();
    // Guideline 速度曲线（简化）
    // Level 0: 1000ms, Level 1: 800ms, ... Level 9: 100ms, 之后 50ms
    if (level >= 9) return 50;
    return 1000 - level * 100;
}

// ============================================================
//  输入处理
// ============================================================

bool PlayerState::isOnGround() const
{
    return board.collides(currentPiece.moved(0, -1));
}

void PlayerState::processInput()
{
    if (gameOver) return;

    TickType_t now = xTaskGetTickCount();

    // --- 左右移动 (DAS/ARR) ---
    // Left DAS
    if (keyLeft) {
        if (dasTimer == 0) {
            movePiece(-1, 0);
            dasTimer = now;
            arrTimer = now;
        } else {
            TickType_t elapsed = now - dasTimer;
            if (elapsed >= pdMS_TO_TICKS(DAS_DELAY_MS)) {
                TickType_t arrElapsed = now - arrTimer;
                if (arrElapsed >= pdMS_TO_TICKS(ARR_RATE_MS)) {
                    movePiece(-1, 0);
                    arrTimer = now;
                }
            }
        }
    } else {
        dasTimer = 0;
        arrTimer = 0;
    }

    // Right DAS (复用 dasTimer — 左右不同时按)
    if (keyRight) {
        if (dasTimer == 0) {
            movePiece(+1, 0);
            dasTimer = now;
            arrTimer = now;
        } else {
            TickType_t elapsed = now - dasTimer;
            if (elapsed >= pdMS_TO_TICKS(DAS_DELAY_MS)) {
                TickType_t arrElapsed = now - arrTimer;
                if (arrElapsed >= pdMS_TO_TICKS(ARR_RATE_MS)) {
                    movePiece(+1, 0);
                    arrTimer = now;
                }
            }
        }
    } else if (!keyLeft) {
        // 只有在左右都没按时才重置（避免右按重置左 DAS）
        // 实际上上面 left 已经处理了 dasTimer，这里留给 right 单独处理
    }

    // --- 网络请求（防吞：即使 keyLeft/keyRight 已被网络延迟的清零消息覆盖，
    //     这里的计数仍能确保移动执行）---
    if (netLeftReq > 0) { movePiece(-1, 0); netLeftReq--; }
    if (netRightReq > 0) { movePiece(+1, 0); netRightReq--; }

    // --- 旋转 ---
    if (keyCW) {
        rotateCW();
        keyCW = false;
    }
    if (keyCCW) {
        rotateCCW();
        keyCCW = false;
    }

    // --- 硬降 ---
    if (keyHard) {
        hardDrop();
        keyHard = false;
    }

    // --- Hold ---
    if (keyHold) {
        doHold();
        keyHold = false;
    }
}

// ============================================================
//  游戏帧更新
// ============================================================

void PlayerState::updateGame()
{
    if (gameOver) return;

    TickType_t now = xTaskGetTickCount();

    // 软降 / 重力（下落失败不锁定，交给 Lock Delay 处理）
    if (keySoft) {
        if (now - gravityTimer >= pdMS_TO_TICKS(SOFT_DROP_MS)) {
            if (movePiece(0, -1)) {
                scoring.processLines(0, false, false, 1, 0);
            }
            gravityTimer = now;
        }
    } else {
        if (now - gravityTimer >= pdMS_TO_TICKS(gravityInterval)) {
            movePiece(0, -1);  // 失败 = 触底，等待 Lock Delay
            gravityTimer = now;
        }
    }

    // Lock Delay
    if (isOnGround()) {
        if (lockTimer == 0)
            lockTimer = now;
        else if (now - lockTimer >= pdMS_TO_TICKS(LOCK_DELAY_MS))
            lockPiece();
    } else {
        lockTimer = 0;
    }

    // 更新 Ghost
    ghostPiece = calculateGhost(currentPiece, board);
}
