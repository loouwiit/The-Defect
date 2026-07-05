#include "chineseChessApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "app/appStackManager.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include <string>
#include <cstdio>

ChineseChessApp::ChineseChessApp(Display* d) : App(d) {}
ChineseChessApp::~ChineseChessApp() = default;

void ChineseChessApp::init()
{
    App::init();
    auto g = display->lockGuard();
    if (!g) { ESP_LOGE(TAG, "lock fail"); return; }

    lv_obj_set_style_bg_color(screen, CBG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    bld();
    refr();
    stat();
    cap();
    ESP_LOGI(TAG, "init %dx%d", lv_obj_get_width(screen), lv_obj_get_height(screen));
}

void ChineseChessApp::deinit() { clr(); App::deinit(); }

// ════════════════════════════════════════════ 构建
void ChineseChessApp::bld()
{
    // 标题
    m_title = lv_label_create(screen);
    lv_obj_set_pos(m_title, 0, 2); lv_obj_set_width(m_title, 720);
    lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(m_title, "♚ 中国象棋");
    lv_obj_set_style_text_color(m_title, CW, 0);
    lv_obj_set_style_text_font(m_title, FontLoader::getDefault(), 0);

    // 走棋提示
    m_turn = lv_label_create(screen);
    lv_obj_set_pos(m_turn, 0, 24); lv_obj_set_width(m_turn, 720);
    lv_obj_set_style_text_align(m_turn, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(m_turn, "红方走棋");
    lv_obj_set_style_text_color(m_turn, GUI::Color::SUBTLE, 0);
    lv_obj_set_style_text_font(m_turn, FontLoader::getDefault(), 0);

    // 棋盘面板
    m_panel = lv_obj_create(screen);
    lv_obj_set_pos(m_panel, BOARD_LEFT - 12, BOARD_TOP - 12);
    lv_obj_set_size(m_panel, BOARD_W + 24, BOARD_H + 24);
    lv_obj_set_style_bg_color(m_panel, CBRD, 0);
    lv_obj_set_style_bg_opa(m_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_panel, 6, 0);
    lv_obj_set_style_border_width(m_panel, 2, 0);
    lv_obj_set_style_border_color(m_panel, CLN, 0);
    lv_obj_set_style_shadow_width(m_panel, 10, 0);
    lv_obj_set_style_shadow_color(m_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(m_panel, LV_OPA_50, 0);
    lv_obj_set_scrollbar_mode(m_panel, LV_SCROLLBAR_MODE_OFF);

    draw();

    // 点击区
    lv_obj_clear_flag(m_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m_panel, ocb, LV_EVENT_CLICKED, this);

    // 选中标记
    m_mk = lv_obj_create(screen);
    lv_obj_set_size(m_mk, PIECE_SIZE + 6, PIECE_SIZE + 6);
    lv_obj_set_style_radius(m_mk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(m_mk, 3, 0);
    lv_obj_set_style_border_color(m_mk, CS, 0);
    lv_obj_set_style_bg_opa(m_mk, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(m_mk, LV_OBJ_FLAG_HIDDEN);

    // 左侧信息
    int ly = 50;
    m_cr = lv_label_create(screen);
    lv_obj_set_pos(m_cr, 4, ly); lv_obj_set_width(m_cr, 64);
    lv_obj_set_style_text_color(m_cr, GUI::Color::SUBTLE, 0);
    lv_obj_set_style_text_font(m_cr, FontLoader::getDefault(), 0);

    ly += 24;
    m_cb = lv_label_create(screen);
    lv_obj_set_pos(m_cb, 4, ly); lv_obj_set_width(m_cb, 64);
    lv_obj_set_style_text_color(m_cb, GUI::Color::SUBTLE, 0);
    lv_obj_set_style_text_font(m_cb, FontLoader::getDefault(), 0);

    ly += 30;
    m_st = lv_label_create(screen);
    lv_obj_set_pos(m_st, 4, ly); lv_obj_set_width(m_st, 64);
    lv_obj_set_style_text_color(m_st, GUI::Color::WARNING, 0);
    lv_obj_set_style_text_font(m_st, FontLoader::getDefault(), 0);

    // 底部按钮
    int bx = 40, by = BOARD_TOP + BOARD_H + 14;
    auto mkbtn = [&](const char* t, lv_event_cb_t cb, int x) {
        auto* b = GUI::createButton(screen, t, 88, 36);
        lv_obj_set_pos(b, x, by);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
        return b;
    };
    m_u = mkbtn("悔棋", oud, bx);
    m_r = mkbtn("重开", ors, bx + 96);
    m_b = mkbtn("返回", obk, bx + 192);
    lv_obj_set_style_bg_color(m_b, GUI::Color::DANGER, 0);

    // 胜利弹窗
    m_win = lv_obj_create(screen);
    lv_obj_set_size(m_win, 300, 140); lv_obj_center(m_win);
    lv_obj_set_style_bg_color(m_win, GUI::Color::CARD, 0);
    lv_obj_set_style_bg_opa(m_win, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_win, 14, 0);
    lv_obj_set_style_border_width(m_win, 0, 0);
    lv_obj_set_style_shadow_width(m_win, 18, 0);
    lv_obj_set_style_shadow_color(m_win, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(m_win, LV_OPA_70, 0);
    lv_obj_add_flag(m_win, LV_OBJ_FLAG_HIDDEN);

    auto wt = lv_label_create(m_win);
    lv_obj_center(wt);
    lv_obj_set_style_text_align(wt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(wt, CW, 0);
    lv_obj_set_style_text_font(wt, FontLoader::getDefault(), 0);
    lv_label_set_text(wt, "将杀！\n红方获胜！");

    auto wr = GUI::createButton(m_win, "再来一局", 100, 32);
    lv_obj_align(wr, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(wr, ors, LV_EVENT_CLICKED, this);
}

// ════════════════════════════════════════════ 网格
void ChineseChessApp::draw()
{
    static const int O = 12;

    auto ln = [this](int x1, int y1, int x2, int y2) {
        auto* p = new lv_point_precise_t[2]{ {x1,y1},{x2,y2} };
        auto* l = lv_line_create(m_panel);
        lv_line_set_points(l, p, 2);
        lv_obj_set_style_line_color(l, CLN, 0);
        lv_obj_set_style_line_width(l, 1, 0);
    };

    // 横线（行, 10条）
    for (int r = 0; r < 10; ++r) {
        int y = O + r * CELL;
        ln(O, y, O + BOARD_W, y);
    }

    // 竖线（列, 9条, 中间列楚河断开）
    for (int c = 0; c < 9; ++c) {
        int x = O + c * CELL;
        if (c == 0 || c == 8)
            ln(x, O, x, O + BOARD_H);
        else {
            ln(x, O, x, O + 4 * CELL);
            ln(x, O + 5 * CELL, x, O + BOARD_H);
        }
    }

    // 九宫斜线
    ln(O + 3 * CELL, O, O + 5 * CELL, O + 2 * CELL);
    ln(O + 5 * CELL, O, O + 3 * CELL, O + 2 * CELL);
    ln(O + 3 * CELL, O + 7 * CELL, O + 5 * CELL, O + 9 * CELL);
    ln(O + 5 * CELL, O + 7 * CELL, O + 3 * CELL, O + 9 * CELL);

    // 楚河汉界
    auto rl = lv_label_create(m_panel);
    lv_obj_set_pos(rl, O + 4, O + 4 * CELL + 4);
    lv_label_set_text(rl, "楚  河");
    lv_obj_set_style_text_color(rl, CLN, 0);
    lv_obj_set_style_text_opa(rl, LV_OPA_20, 0);

    auto rr = lv_label_create(m_panel);
    lv_obj_set_pos(rr, O + BOARD_W - 60, O + 4 * CELL + 4);
    lv_label_set_text(rr, "汉  界");
    lv_obj_set_style_text_color(rr, CLN, 0);
    lv_obj_set_style_text_opa(rr, LV_OPA_20, 0);
}

// ════════════════════════════════════════════ 棋子
void ChineseChessApp::clr()
{
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 9; ++c) {
            if (m_pc[r][c]) { lv_obj_delete(m_pc[r][c]); m_pc[r][c] = nullptr; }
            if (m_dot[r][c]) { lv_obj_delete(m_dot[r][c]); m_dot[r][c] = nullptr; }
        }
}

void ChineseChessApp::refr()
{
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 9; ++c)
            upd(r, c);
}

void ChineseChessApp::upd(int r, int c)
{
    const auto& p = m_logic.at(r, c);
    if (p.isEmpty()) {
        if (m_pc[r][c]) { lv_obj_delete(m_pc[r][c]); m_pc[r][c] = nullptr; }
        return;
    }

    if (!m_pc[r][c]) {
        auto* o = lv_obj_create(m_panel);
        lv_obj_set_size(o, PIECE_SIZE, PIECE_SIZE);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(o, 0, 0);
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_shadow_width(o, 4, 0);
        lv_obj_set_style_shadow_color(o, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(o, LV_OPA_50, 0);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(o, ocb, LV_EVENT_CLICKED, this);

        auto* l = lv_label_create(o);
        lv_obj_center(l);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, FontLoader::getDefault(), 0);
        m_pc[r][c] = o;
    }

    auto* o = m_pc[r][c];
    auto* l = static_cast<lv_obj_t*>(lv_obj_get_child(o, 0));

    // 面板局部坐标
    static const int O = 12;
    lv_obj_set_pos(o, O + c * CELL - PIECE_SIZE / 2,
                       O + r * CELL - PIECE_SIZE / 2);

    bool red = p.side == ChineseChessLogic::Side::Red;
    lv_obj_set_style_bg_color(o, red ? CR : CB, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, red ? CR : CB, 0);
    lv_obj_set_style_text_color(l, CW, 0);
    lv_label_set_text(l, ChineseChessLogic::getPieceName(p.type, p.side));
    lv_obj_center(l);
}

// ════════════════════════════════════════════ 选中/提示
void ChineseChessApp::sel()
{
    static const int O = 12;
    if (m_sr >= 0 && m_sc >= 0) {
        lv_obj_set_pos(m_mk, O + m_sc * CELL - (PIECE_SIZE + 6) / 2,
                            O + m_sr * CELL - (PIECE_SIZE + 6) / 2);
        lv_obj_clear_flag(m_mk, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(m_mk);
    } else {
        lv_obj_add_flag(m_mk, LV_OBJ_FLAG_HIDDEN);
    }
}

void ChineseChessApp::hint()
{
    for (int r = 0; r < 10; ++r)
        for (int c = 0; c < 9; ++c)
            if (m_dot[r][c]) lv_obj_add_flag(m_dot[r][c], LV_OBJ_FLAG_HIDDEN);

    static const int O = 12;
    for (auto& mv : m_moves) {
        int r = mv.first, c = mv.second;
        if (!m_dot[r][c]) {
            m_dot[r][c] = lv_obj_create(m_panel);
            lv_obj_set_size(m_dot[r][c], 14, 14);
            lv_obj_set_style_radius(m_dot[r][c], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(m_dot[r][c], CH, 0);
            lv_obj_set_style_bg_opa(m_dot[r][c], LV_OPA_60, 0);
            lv_obj_set_style_border_width(m_dot[r][c], 0, 0);
            lv_obj_clear_flag(m_dot[r][c], LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_set_pos(m_dot[r][c], O + c * CELL - 7, O + r * CELL - 7);
        lv_obj_clear_flag(m_dot[r][c], LV_OBJ_FLAG_HIDDEN);
    }
}

// ════════════════════════════════════════════ 状态
void ChineseChessApp::stat()
{
    auto t = m_logic.currentTurn();
    lv_label_set_text(m_turn, t == ChineseChessLogic::Side::Red ? "红方走棋" : "黑方走棋");
    if (m_logic.isCheckmate(t)) {
        auto w = t == ChineseChessLogic::Side::Red ? ChineseChessLogic::Side::Black : ChineseChessLogic::Side::Red;
        lv_label_set_text(m_st, "将杀！"); win(w);
    } else if (m_logic.isStalemate(t)) {
        auto w = t == ChineseChessLogic::Side::Red ? ChineseChessLogic::Side::Black : ChineseChessLogic::Side::Red;
        lv_label_set_text(m_st, "困毙！"); win(w);
    } else if (m_logic.isInCheck(t)) {
        lv_label_set_text(m_st, "将军！");
    } else {
        lv_label_set_text(m_st, "");
    }
}

void ChineseChessApp::cap()
{
    std::string r = "紅:";
    for (auto& p : m_logic.capturedByRed()) r += ChineseChessLogic::getPieceName(p.type, p.side);
    lv_label_set_text(m_cr, r.c_str());
    std::string b = "黑:";
    for (auto& p : m_logic.capturedByBlack()) b += ChineseChessLogic::getPieceName(p.type, p.side);
    lv_label_set_text(m_cb, b.c_str());
}

void ChineseChessApp::win(ChineseChessLogic::Side w)
{
    auto* t = static_cast<lv_obj_t*>(lv_obj_get_child(m_win, 0));
    if (t) { char b[32]; snprintf(b, sizeof(b), "将杀！\n%s获胜！",
        w == ChineseChessLogic::Side::Red ? "红方" : "黑方"); lv_label_set_text(t, b); }
    lv_obj_clear_flag(m_win, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(m_win);
}

void ChineseChessApp::hwin() { lv_obj_add_flag(m_win, LV_OBJ_FLAG_HIDDEN); }

// ════════════════════════════════════════════ 点击
void ChineseChessApp::ocb(lv_event_t* e)
{
    auto* s = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
    if (!lv_obj_has_flag(s->m_win, LV_OBJ_FLAG_HIDDEN)) return;

    // 触摸坐标 -> 面板局部坐标
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    int lx = pt.x - lv_obj_get_x(s->m_panel);
    int ly = pt.y - lv_obj_get_y(s->m_panel);

    static const int O = 12;
    int cc = (lx - O + CELL / 2) / CELL;
    int rr = (ly - O + CELL / 2) / CELL;
    if (rr < 0 || rr > 9 || cc < 0 || cc > 8) return;

    const auto& clk = s->m_logic.at(rr, cc);

    if (s->m_sr < 0) {
        if (clk.isEmpty() || clk.side != s->m_logic.currentTurn()) return;
        s->m_sr = rr; s->m_sc = cc;
        s->m_moves = s->m_logic.getValidMoves(rr, cc);
        s->sel(); s->hint(); return;
    }

    if (rr == s->m_sr && cc == s->m_sc) {
        s->m_sr = -1; s->m_sc = -1; s->m_moves.clear();
        s->sel(); s->hint(); return;
    }

    if (!clk.isEmpty() && clk.side == s->m_logic.currentTurn()) {
        s->m_sr = rr; s->m_sc = cc;
        s->m_moves = s->m_logic.getValidMoves(rr, cc);
        s->sel(); s->hint(); return;
    }

    if (s->m_logic.makeMove(s->m_sr, s->m_sc, rr, cc)) {
        s->m_sr = -1; s->m_sc = -1; s->m_moves.clear();
        s->refr(); s->sel(); s->hint();
        s->cap(); s->stat();
    }
}

void ChineseChessApp::oud(lv_event_t* e)
{
    auto* s = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
    if (s->m_logic.moveCount() == 0) return;
    s->m_logic.undoLastMove();
    s->m_sr = -1; s->m_sc = -1; s->m_moves.clear();
    s->hwin();
    s->refr(); s->sel(); s->hint();
    s->cap(); s->stat();
}

void ChineseChessApp::ors(lv_event_t* e)
{
    auto* s = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
    s->m_logic.reset();
    s->m_sr = -1; s->m_sc = -1; s->m_moves.clear();
    s->hwin();
    s->refr(); s->sel(); s->hint();
    s->stat(); s->cap();
}

void ChineseChessApp::obk(lv_event_t* e)
{
    auto* s = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
    Task::addTask([](void* p) -> TickType_t {
        static_cast<ChineseChessApp*>(p)->popApp();
        return Task::infinityTime;
    }, "cBk", s, 0, Task::Affinity::None);
}
