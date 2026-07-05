#pragma once

#include "app/app.hpp"
#include "chineseChessLogic.hpp"
#include <vector>
#include <utility>

class ChineseChessApp final : public App
{
public:
	static constexpr char TAG[] = "ChineseChessApp";

	ChineseChessApp(Display* display);
	~ChineseChessApp() override;

	void init() override;
	void deinit() override;

private:
	static constexpr int CELL = 72;
	static constexpr int BOARD_W = 8 * CELL; // 576
	static constexpr int BOARD_H = 9 * CELL; // 648
	static constexpr int BOARD_LEFT = (720 - BOARD_W) / 2; // 72
	static constexpr int BOARD_TOP = 44;
	static constexpr int PIECE_SIZE = 56;

	static constexpr lv_color_t CR = LV_COLOR_MAKE(0xCC,0x22,0x22);
	static constexpr lv_color_t CB = LV_COLOR_MAKE(0x2A,0x2A,0x2A);
	static constexpr lv_color_t CW = LV_COLOR_MAKE(0xFF,0xFF,0xFF);
	static constexpr lv_color_t CS = LV_COLOR_MAKE(0x00,0xCC,0x44);
	static constexpr lv_color_t CH = LV_COLOR_MAKE(0x44,0xBB,0x44);
	static constexpr lv_color_t CBRD = LV_COLOR_MAKE(0xDE,0xB8,0x87);
	static constexpr lv_color_t CLN = LV_COLOR_MAKE(0x33,0x22,0x11);
	static constexpr lv_color_t CBG = LV_COLOR_MAKE(0x1a,0x1a,0x2e);

	ChineseChessLogic m_logic;
	int m_sr{-1}, m_sc{-1};
	std::vector<std::pair<int,int>> m_moves;

	lv_obj_t* m_panel{};
	lv_obj_t* m_pc[10][9]{};
	lv_obj_t* m_mk{};
	lv_obj_t* m_dot[10][9]{};
	lv_obj_t* m_title{};
	lv_obj_t* m_turn{};
	lv_obj_t* m_cr{};
	lv_obj_t* m_cb{};
	lv_obj_t* m_st{};
	lv_obj_t* m_u{}, *m_r{}, *m_b{};
	lv_obj_t* m_win{};

	void bld();
	void draw();
	void refr();
	void upd(int r, int c);
	void sel();
	void hint();
	void stat();
	void cap();
	void win(ChineseChessLogic::Side w);
	void hwin();
	void clr();

	static void ocb(lv_event_t*);
	static void oud(lv_event_t*);
	static void ors(lv_event_t*);
	static void obk(lv_event_t*);
};
