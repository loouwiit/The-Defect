#include "inputer.hpp"
#include <cstdlib>

// ============================================================
// DirectionKeyInputer
// ============================================================

void DirectionKeyInputer::push(GameAction action)
{
	int next = (m_tail + 1) % QUEUE_SIZE;
	if (next == m_head)
	{
		// 队列满，丢弃最旧
		m_head = (m_head + 1) % QUEUE_SIZE;
	}
	m_queue[m_tail] = action;
	m_tail = next;
}

GameAction DirectionKeyInputer::getAction()
{
	if (m_head == m_tail) return GameAction::None;
	GameAction action = m_queue[m_head];
	m_head = (m_head + 1) % QUEUE_SIZE;
	return action;
}

void DirectionKeyInputer::reset()
{
	m_head = m_tail = 0;
}

void DirectionKeyInputer::onUp()    { push(GameAction::Up); }
void DirectionKeyInputer::onDown()  { push(GameAction::Down); }
void DirectionKeyInputer::onLeft()  { push(GameAction::Left); }
void DirectionKeyInputer::onRight() { push(GameAction::Right); }
void DirectionKeyInputer::onConfirm() { push(GameAction::Confirm); }
void DirectionKeyInputer::onCancel()  { push(GameAction::Cancel); }

// ============================================================
// TouchSwipeInputer
// ============================================================

TouchSwipeInputer::TouchSwipeInputer()
{
}

void TouchSwipeInputer::push(GameAction action)
{
	int next = (m_tail + 1) % QUEUE_SIZE;
	if (next == m_head)
	{
		m_head = (m_head + 1) % QUEUE_SIZE;
	}
	m_queue[m_tail] = action;
	m_tail = next;
}

GameAction TouchSwipeInputer::getAction()
{
	if (m_head == m_tail) return GameAction::None;
	GameAction action = m_queue[m_head];
	m_head = (m_head + 1) % QUEUE_SIZE;
	return action;
}

void TouchSwipeInputer::reset()
{
	m_head = m_tail = 0;
	m_touching = false;
	m_inDpad = false;
}

void TouchSwipeInputer::setDpadZone(int16_t cx, int16_t cy, int16_t halfSize)
{
	m_dpadCX = cx;
	m_dpadCY = cy;
	m_dpadHalf = halfSize;
}

GameAction TouchSwipeInputer::posToDir(int16_t x, int16_t y) const
{
	// 相对于 D-pad 中心的偏移
	int16_t dx = x - m_dpadCX;
	int16_t dy = y - m_dpadCY;

	// 死区
	if (abs(dx) < 20 && abs(dy) < 20) return GameAction::None;

	if (abs(dx) > abs(dy))
		return dx > 0 ? GameAction::Right : GameAction::Left;
	else
		return dy > 0 ? GameAction::Down : GameAction::Up;
}

void TouchSwipeInputer::onTouchDown(int16_t x, int16_t y)
{
	m_downX = x;
	m_downY = y;
	m_touching = true;

	// 检查是否在 D-pad 区域内
	if (m_dpadHalf > 0)
	{
		int16_t dx = x - m_dpadCX;
		int16_t dy = y - m_dpadCY;
		m_inDpad = (abs(dx) <= m_dpadHalf && abs(dy) <= m_dpadHalf);
		if (m_inDpad)
		{
			GameAction dir = posToDir(x, y);
			if (dir != GameAction::None)
				push(dir);
		}
	}
}

void TouchSwipeInputer::onTouchMove(int16_t x, int16_t y)
{
	if (!m_touching) return;

	// 在 D-pad 区域内时，手指移动触发新方向
	if (m_inDpad && m_dpadHalf > 0)
	{
		GameAction dir = posToDir(x, y);
		if (dir != GameAction::None)
			push(dir);
	}
}

void TouchSwipeInputer::onTouchUp(int16_t x, int16_t y)
{
	if (!m_touching) return;
	m_touching = false;
	m_inDpad = false;

	// 如果不在 D-pad 区域且移动距离足够，视为滑动手势
	if (!m_inDpad && m_dpadHalf > 0)
	{
		int16_t dx = x - m_downX;
		int16_t dy = y - m_downY;

		if (abs(dx) > SWIPE_THRESHOLD || abs(dy) > SWIPE_THRESHOLD)
		{
			if (abs(dx) > abs(dy))
				push(dx > 0 ? GameAction::Right : GameAction::Left);
			else
				push(dy > 0 ? GameAction::Down : GameAction::Up);
		}
	}
}
