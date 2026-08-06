#pragma once

#include "touch/touch.hpp"
#include <cstddef>

/**
 * @brief 逐指状态机：把 GT911 触点快照（含 track_id）转成每根手指的稳定状态
 *
 * 按 track_id 匹配同一根手指，维护 pressed / justPressed / justReleased + 坐标。
 * 手指松开后 track_id 重按视为新按下（justPressed）。
 * 纯状态机，无任务/无全局，供 TouchService 内部使用。
 */
class MultiTouchTracker
{
public:
	static constexpr int MaxFingers = 5;

	struct Finger
	{
		int id = -1;               // 手指 id（GT911 track_id）
		bool pressed = false;      // 当前是否按住
		bool justPressed = false;  // 本帧新按下（上升沿）
		bool justReleased = false; // 本帧新松开（下降沿，保留一帧）
		bool present = false;      // 本帧硬件是否上报该触点
		int16_t x = 0, y = 0;      // 屏幕坐标
		uint8_t strength = 0;      // 压力
	};

	/// 输入一帧触点快照，更新所有手指状态
	void update(const Touch::PointData* points, size_t count)
	{
		// 重置本帧事件标记
		for (auto& f : m_fingers)
		{
			f.justPressed = false;
			f.justReleased = false;
			f.present = false;
		}

		const size_t n = (count > MaxFingers) ? MaxFingers : count;
		for (size_t i = 0; i < n; i++)
		{
			const uint8_t tid = points[i].track_id;
			int slot = indexByTrackId(tid);
			if (slot < 0)
			{
				slot = firstFreeSlot();
				if (slot < 0) continue;
				auto& f = m_fingers[slot];
				f.id = tid;
				f.justReleased = false;   // 复用释放槽位时清掉旧释放标记
				f.pressed = false;
			}
			auto& f = m_fingers[slot];
			if (!f.present)
			{
				if (!f.pressed) f.justPressed = true;
				f.pressed = true;
				f.present = true;
			}
			f.x = (int16_t)points[i].x;
			f.y = (int16_t)points[i].y;
			f.strength = points[i].strength;
		}

		// 释放检测：上一帧按住、本帧无触点 → 松开（保留一帧以便读取）
		for (auto& f : m_fingers)
		{
			if (f.pressed && !f.present)
			{
				f.justReleased = true;
				f.pressed = false;
			}
		}

		// 活跃计数（按住中 或 本帧刚释放）
		m_count = 0;
		for (const auto& f : m_fingers)
			if (f.pressed || f.justReleased) m_count++;
	}

	/// 活跃手指数
	int fingerCount() const { return m_count; }

	/// 按列表索引取手指（顺序不保证稳定，用 id 区分）
	const Finger* fingerAt(int index) const
	{
		if (index < 0 || index >= m_count) return nullptr;
		int seen = 0;
		for (const auto& f : m_fingers)
		{
			if (f.pressed || f.justReleased)
			{
				if (seen == index) return &f;
				seen++;
			}
		}
		return nullptr;
	}

	/// 按手指 id（track_id）查找
	const Finger* fingerById(int id) const
	{
		for (const auto& f : m_fingers)
			if (f.id == id && (f.pressed || f.justReleased)) return &f;
		return nullptr;
	}

	/// 重置所有手指状态
	void reset()
	{
		for (auto& f : m_fingers) f = Finger{};
		m_count = 0;
	}

private:
	int indexByTrackId(uint8_t tid) const
	{
		for (int i = 0; i < MaxFingers; i++)
			if (m_fingers[i].pressed && m_fingers[i].id == tid) return i;
		return -1;
	}

	int firstFreeSlot()
	{
		for (int i = 0; i < MaxFingers; i++)
			if (!m_fingers[i].pressed) return i;
		return -1;
	}

	Finger m_fingers[MaxFingers];
	int m_count = 0;
};
