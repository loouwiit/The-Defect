#pragma once

#include <cstdint>
#include <deque>

/**
 * @brief 贪吃蛇游戏核心逻辑（纯数据，无 LVGL 依赖）
 *
 * 遵循 README 设计：
 * "游戏行为由事件驱动，仅由主机发送事件。"
 *
 * 所有游戏状态都在此管理，外部通过 tick() 推进。
 * 输入由 Inputer 提供，游戏逻辑不关心输入来源。
 */
class SnakeGameLogic
{
public:
	/** @brief 获取网格尺寸（由构造时传入） */
	int getGridW() const { return m_gridW; }
	int getGridH() const { return m_gridH; }

	/** @brief 网格单元格值 */
	enum Cell : uint8_t
	{
		CELL_EMPTY  = 0,
		CELL_SNAKE1 = 1,   // 玩家 1 蛇身
		CELL_SNAKE2 = 2,   // 玩家 2 蛇身
		CELL_FOOD   = 3,
		CELL_SNAKE3 = 4,   // 玩家 3 蛇身
	};

	enum class Direction : uint8_t
	{
		None,
		Up,
		Down,
		Left,
		Right,
	};

	struct Position
	{
		int8_t x, y;
		bool operator==(const Position& o) const { return x == o.x && y == o.y; }
		bool operator!=(const Position& o) const { return x != o.x || y != o.y; }
	};

	struct Snake
	{
		std::deque<Position> body;  // body[0] = 蛇头
		Direction direction = Direction::Right;
		Direction pendingDir = Direction::Right;
		bool alive = true;
		int score = 0;
	};

	/** @brief 游戏状态 */
	enum class State : uint8_t
	{
		Waiting,    // 等待开始
		Playing,    // 游戏中
		Paused,     // 暂停
		GameOver,   // 游戏结束
	};

	SnakeGameLogic(int playerCount = 1, int gridW = 36, int gridH = 55);
	~SnakeGameLogic() = default;

	/** @brief 重置游戏 */
	void reset();

	/** @brief 推进一帧（由定时器驱动），返回 true 表示游戏仍在进行 */
	bool tick();

	/** @brief 设置玩家方向 */
	void setDirection(int player, Direction dir);

	/** @brief 获取玩家方向 */
	Direction getDirection(int player) const;

	/** @brief 获取蛇 */
	const Snake& getSnake(int player) const;

	/** @brief 获取食物位置 */
	const Position& getFood() const { return m_food; }

	/** @brief 获取网格值 */
	Cell getCell(int x, int y) const;

	/** @brief 获取网格值（通过位置） */
	Cell getCell(Position p) const { return getCell(p.x, p.y); }

	/** @brief 游戏状态 */
	State getState() const { return m_state; }

	/** @brief 设置游戏状态 */
	void setState(State state) { m_state = state; }

	/** @brief 玩家数量 */
	int getPlayerCount() const { return m_playerCount; }

	/** @brief 当前步数（用于速度递增） */
	int getStepCount() const { return m_stepCount; }

	/** @brief 获取某个玩家的输入方向队列是否不为空 */
	bool hasPendingDirection(int player) const;

private:
	static constexpr int MAX_PLAYERS = 3;

	int m_playerCount;
	int m_gridW, m_gridH;
	Snake m_snakes[MAX_PLAYERS];
	Position m_food{};
	State m_state = State::Waiting;
	int m_stepCount = 0;

	void spawnFood();
	bool checkCollision(const Position& head, int player, bool excludeTail) const;
};

/** @brief 方向工具函数 */
namespace DirUtils {
	SnakeGameLogic::Direction opposite(SnakeGameLogic::Direction d);
	SnakeGameLogic::Direction clockwise(SnakeGameLogic::Direction d);
	SnakeGameLogic::Direction counterClockwise(SnakeGameLogic::Direction d);
}
