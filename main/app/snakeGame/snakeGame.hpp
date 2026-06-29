#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "gameLogic.hpp"

/**
 * @brief 贪吃蛇游戏 App
 *
 * 支持：
 *   - 1~2 人本地游戏（同一屏幕双 D-pad）
 *   - 触摸屏 D-pad 控制（玩家 1 右 / 玩家 2 左）
 *   - 屏幕串流远程触控（远程触摸 → VirtualIndev → LVGL 按钮）
 *   - 远程 WebSocket 方向键（/ws/game）
 *   - LVGL 原生对象渲染（预分配对象池，脏区域追踪）
 *   - 自动适配屏幕串流（ScreenStream 自动捕获 LVGL 画面）
 *   - 动态适配屏幕分辨率
 */
class SnakeGame : public App
{
public:
	static constexpr char TAG[] = "SnakeGame";

	SnakeGame(Display* display);
	~SnakeGame() override;

	void init() override;
	void deinit() override;

	/** @brief WebSocket 游戏按键回调（静态，转发给实例） */
	static void gameKeyCb(int player, uint8_t keyCode, bool pressed, void* ctx);

private:
	// 游戏核心
	SnakeGameLogic m_logic;
	int m_playerCount = 1;  // 实际玩家数（从选择界面设置）

	// 游戏循环线程
	Thread m_thread;

	// 对象池常量
	static constexpr int MAX_SEGMENTS = 64;
	static constexpr int MAX_FOOD_ITEMS = 8;
	static constexpr int MAX_PLAYERS = 3;

	// LVGL 游戏对象池
	lv_obj_t* m_segments[MAX_PLAYERS][MAX_SEGMENTS]{};
	int m_segmentCount[MAX_PLAYERS]{};
	lv_obj_t* m_foodItems[MAX_FOOD_ITEMS]{};
	int m_foodCount = 1;

	// 动态尺寸（从屏幕分辨率计算）
	int m_cellSize = 20;
	int m_gridW = 36;
	int m_gridH = 55;

	lv_obj_t* m_scoreLabel = nullptr;
	lv_obj_t* m_statusLabel = nullptr;
	lv_obj_t* m_gameOverLabel = nullptr;
	lv_obj_t* m_gameOverScoreLabel = nullptr;
	lv_obj_t* m_restartBtn = nullptr;
	lv_obj_t* m_backBtn = nullptr;

	// 模式选择界面
	lv_obj_t* m_menu = nullptr;
	lv_obj_t* m_btn1P = nullptr;
	lv_obj_t* m_btn2P = nullptr;
	lv_obj_t* m_btn3P = nullptr;

	// D-pad 容器（玩家 1 右侧 / 玩家 2 左侧 / 玩家 3 中间）
	lv_obj_t* m_pad1 = nullptr;
	lv_obj_t* m_pad2 = nullptr;
	lv_obj_t* m_pad3 = nullptr;

	// 玩家 1 D-pad 按钮
	lv_obj_t* m_p1Up = nullptr;
	lv_obj_t* m_p1Down = nullptr;
	lv_obj_t* m_p1Left = nullptr;
	lv_obj_t* m_p1Right = nullptr;
	lv_obj_t* m_p1Pause = nullptr;

	// 玩家 2 D-pad 按钮
	lv_obj_t* m_p2Up = nullptr;
	lv_obj_t* m_p2Down = nullptr;
	lv_obj_t* m_p2Left = nullptr;
	lv_obj_t* m_p2Right = nullptr;

	// 玩家 3 D-pad 按钮
	lv_obj_t* m_p3Up = nullptr;
	lv_obj_t* m_p3Down = nullptr;
	lv_obj_t* m_p3Left = nullptr;
	lv_obj_t* m_p3Right = nullptr;

	/** @brief 创建模式选择菜单 */
	void createMenu(lv_obj_t* parent);

	/** @brief 开始游戏（选择人数后调用） */
	void startGame(int playerCount);

	/** @brief 创建 D-pad 按钮 */
	void createDpad(lv_obj_t* parent);

	/** @brief 创建游戏对象池（蛇身段 + 食物） */
	void createObjectPool(lv_obj_t* parent);

	/** @brief 渲染一帧 */
	void renderFrame();

	/** @brief 游戏主循环 */
	static void gameLoop(void* param);

	/** @brief D-pad 按钮 LVGL 事件回调 */
	static void btnP1UpCb(lv_event_t* e);
	static void btnP1DownCb(lv_event_t* e);
	static void btnP1LeftCb(lv_event_t* e);
	static void btnP1RightCb(lv_event_t* e);
	static void btnPauseCb(lv_event_t* e);

	static void btnP2UpCb(lv_event_t* e);
	static void btnP2DownCb(lv_event_t* e);
	static void btnP2LeftCb(lv_event_t* e);
	static void btnP2RightCb(lv_event_t* e);

	static void btnP3UpCb(lv_event_t* e);
	static void btnP3DownCb(lv_event_t* e);
	static void btnP3LeftCb(lv_event_t* e);
	static void btnP3RightCb(lv_event_t* e);

	static void btn1PCb(lv_event_t* e);
	static void btn2PCb(lv_event_t* e);
	static void btn3PCb(lv_event_t* e);
	static void btnRestartCb(lv_event_t* e);
	static void btnBackCb(lv_event_t* e);

	/** @brief 返回模式选择菜单 */
	void goBackToMenu();

	/** @brief 设置方向并启动游戏（供回调使用） */
	static void setDirAndStart(SnakeGame* self, int player, SnakeGameLogic::Direction dir);
};
