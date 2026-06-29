#include "gameLogic.hpp"
#include <cstdlib>
#include <ctime>
#include <algorithm>

// ============================================================
// 方向工具
// ============================================================

SnakeGameLogic::Direction DirUtils::opposite(SnakeGameLogic::Direction d)
{
	using D = SnakeGameLogic::Direction;
	switch (d)
	{
	case D::Up:    return D::Down;
	case D::Down:  return D::Up;
	case D::Left:  return D::Right;
	case D::Right: return D::Left;
	default:       return D::None;
	}
}

SnakeGameLogic::Direction DirUtils::clockwise(SnakeGameLogic::Direction d)
{
	using D = SnakeGameLogic::Direction;
	switch (d)
	{
	case D::Up:    return D::Right;
	case D::Right: return D::Down;
	case D::Down:  return D::Left;
	case D::Left:  return D::Up;
	default:       return D::None;
	}
}

SnakeGameLogic::Direction DirUtils::counterClockwise(SnakeGameLogic::Direction d)
{
	using D = SnakeGameLogic::Direction;
	switch (d)
	{
	case D::Up:    return D::Left;
	case D::Left:  return D::Down;
	case D::Down:  return D::Right;
	case D::Right: return D::Up;
	default:       return D::None;
	}
}

// ============================================================
// SnakeGameLogic
// ============================================================

SnakeGameLogic::SnakeGameLogic(int playerCount, int gridW, int gridH)
	: m_playerCount{ playerCount }
	, m_gridW{ gridW }
	, m_gridH{ gridH }
{
	if (m_playerCount < 1) m_playerCount = 1;
	if (m_playerCount > MAX_PLAYERS) m_playerCount = MAX_PLAYERS;
	if (m_gridW < 10) m_gridW = 10;
	if (m_gridH < 10) m_gridH = 10;
	srand(time(nullptr));
	reset();
}

void SnakeGameLogic::reset()
{
	m_state = State::Waiting;
	m_stepCount = 0;

	for (int p = 0; p < m_playerCount; p++)
	{
		auto& snake = m_snakes[p];
		snake.body.clear();
		snake.alive = true;
		snake.score = 0;

		// 玩家 1 从左下角出发，朝右
		if (p == 0)
		{
			// 玩家 1：左中区域出发，朝右
			int8_t startY = m_gridH / 2;
			snake.body.push_back({ 5, startY });
			snake.body.push_back({ 4, startY });
			snake.body.push_back({ 3, startY });
			snake.direction = Direction::Right;
			snake.pendingDir = Direction::Right;
		}
		else if (p == 1)
		{
			// 玩家 2：右中区域出发，朝左
			int8_t startY = m_gridH / 2 + 3;
			snake.body.push_back({ static_cast<int8_t>(m_gridW - 6), startY });
			snake.body.push_back({ static_cast<int8_t>(m_gridW - 5), startY });
			snake.body.push_back({ static_cast<int8_t>(m_gridW - 4), startY });
			snake.direction = Direction::Left;
			snake.pendingDir = Direction::Left;
		}
		else
		{
			// 玩家 3：中上区域出发，朝下
			int8_t startX = m_gridW / 2;
			snake.body.push_back({ startX, 5 });
			snake.body.push_back({ startX, 4 });
			snake.body.push_back({ startX, 3 });
			snake.direction = Direction::Down;
			snake.pendingDir = Direction::Down;
		}
	}

	spawnFood();
}

void SnakeGameLogic::spawnFood()
{
	// 随机尝试找空格（不分配栈上大数组）
	int attempts = 0;
	const int maxAttempts = (m_gridW * m_gridH) * 2;
	while (attempts < maxAttempts)
	{
		int x = rand() % m_gridW;
		int y = rand() % m_gridH;
		if (getCell(x, y) == CELL_EMPTY)
		{
			m_food.x = static_cast<int8_t>(x);
			m_food.y = static_cast<int8_t>(y);
			return;
		}
		attempts++;
	}

	// 兜底：顺序扫描（棋盘几乎满时）
	for (int x = 0; x < m_gridW; x++)
		for (int y = 0; y < m_gridH; y++)
			if (getCell(x, y) == CELL_EMPTY)
			{
				m_food.x = static_cast<int8_t>(x);
				m_food.y = static_cast<int8_t>(y);
				return;
			}

	// 没有空格，棋盘满了——蛇赢了
	m_food.x = 0;
	m_food.y = 0;
}

bool SnakeGameLogic::checkCollision(const Position& head, int player, bool excludeTail) const
{
	// 墙壁碰撞
	if (head.x < 0 || head.x >= m_gridW || head.y < 0 || head.y >= m_gridH)
		return true;

	// 自身碰撞（撞身体，允许撞尾巴（因为尾巴将在本帧被移除））
	const auto& snake = m_snakes[player];
	for (size_t i = 0; i < snake.body.size(); i++)
	{
		if (excludeTail && i == snake.body.size() - 1) continue; // 尾巴
		if (snake.body[i].x == head.x && snake.body[i].y == head.y)
			return true;
	}

	// 多人：撞其他玩家
	for (int p = 0; p < m_playerCount; p++)
	{
		if (p == player) continue;
		const auto& other = m_snakes[p];
		for (size_t i = 0; i < other.body.size(); i++)
		{
			if (other.body[i].x == head.x && other.body[i].y == head.y)
				return true;
		}
	}

	return false;
}

SnakeGameLogic::Cell SnakeGameLogic::getCell(int x, int y) const
{
	if (x < 0 || x >= m_gridW || y < 0 || y >= m_gridH)
		return CELL_EMPTY;

	if (m_food.x == x && m_food.y == y)
		return CELL_FOOD;

	for (int p = 0; p < m_playerCount; p++)
	{
		for (auto& seg : m_snakes[p].body)
		{
			if (seg.x == x && seg.y == y)
			{
				if (p == 0) return CELL_SNAKE1;
				if (p == 1) return CELL_SNAKE2;
				return CELL_SNAKE3;
			}
		}
	}

	return CELL_EMPTY;
}

void SnakeGameLogic::setDirection(int player, Direction dir)
{
	if (player < 0 || player >= m_playerCount) return;
	if (dir == Direction::None) return;

	// 不允许 180° 掉头
	if (dir == DirUtils::opposite(m_snakes[player].direction))
		return;

	m_snakes[player].pendingDir = dir;
}

SnakeGameLogic::Direction SnakeGameLogic::getDirection(int player) const
{
	if (player < 0 || player >= m_playerCount) return Direction::None;
	return m_snakes[player].direction;
}

const SnakeGameLogic::Snake& SnakeGameLogic::getSnake(int player) const
{
	return m_snakes[player];
}

bool SnakeGameLogic::hasPendingDirection(int player) const
{
	if (player < 0 || player >= m_playerCount) return false;
	return m_snakes[player].direction != m_snakes[player].pendingDir;
}

bool SnakeGameLogic::tick()
{
	if (m_state != State::Playing) return false;

	m_stepCount++;

	// 阶段 1：移动所有蛇，检查碰撞（只标记死亡，不清除身体）
	for (int p = 0; p < m_playerCount; p++)
	{
		auto& snake = m_snakes[p];
		if (!snake.alive) continue;

		// 应用待处理方向
		snake.direction = snake.pendingDir;

		// 计算新蛇头位置
		Position newHead = snake.body.front();
		switch (snake.direction)
		{
		case Direction::Up:    newHead.y -= 1; break;
		case Direction::Down:  newHead.y += 1; break;
		case Direction::Left:  newHead.x -= 1; break;
		case Direction::Right: newHead.x += 1; break;
		default: break;
		}

		// 检查是否吃到食物
		bool ate = (newHead == m_food);

		// 碰撞检测（身体会保留到阶段 2，确保本帧其他蛇也能检测到）
		if (checkCollision(newHead, p, !ate))
		{
			snake.alive = false;
			continue;
		}

		// 移动蛇
		snake.body.push_front(newHead);
		if (!ate)
		{
			snake.body.pop_back();
		}
		else
		{
			snake.score++;
			spawnFood();
		}
	}

	// 阶段 2：清除已死亡蛇的身体（"消失"效果）
	for (int p = 0; p < m_playerCount; p++)
	{
		if (!m_snakes[p].alive)
			m_snakes[p].body.clear();
	}

	// 阶段 3：检查游戏结束条件
	bool anyAlive = false;
	for (int p = 0; p < m_playerCount; p++)
	{
		if (m_snakes[p].alive)
		{
			anyAlive = true;
		}
	}

	if (!anyAlive)
	{
		m_state = State::GameOver;
		return false;
	}

	return true;
}
