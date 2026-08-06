#include "touch/touchService.hpp"
#include "touch/touch.hpp"
#include "touch/touchButton.hpp"
#include "display/display.hpp"
#include "app/app.hpp"
#include "lvgl.h"

static constexpr char TAG[] = "TouchService";

TouchService& TouchService::instance()
{
	static TouchService s_instance;
	return s_instance;
}

void TouchService::init(Touch* touch, Display* display)
{
	m_touch = touch;
	m_display = display;
}

void TouchService::update()
{
	if (!m_touch || !m_display) return;

	// 持 LVGL 锁：保证命中测试期间对象不被删除 + send_event 安全
	auto guard = m_display->lockGuard();
	if (!guard) return;

	Touch::PointData points[MaxFingers]{};
	size_t n = m_touch->getPoints(points, MaxFingers);
	m_tracker.update(points, n);

	Lock lock(m_mutex);

	const auto& tr = m_tracker;
	const int fc = tr.fingerCount();
	for (int i = 0; i < fc; i++)
	{
		const auto& f = *tr.fingerAt(i);

		// 松开：解除锁定 + 通知按钮松开
		if (f.justReleased)
		{
			auto it = m_locked.find(f.id);
			if (it != m_locked.end())
			{
				if (it->second) it->second->serviceSetPressed(false);
				m_locked.erase(it);
			}
			continue;
		}

		// 新按下：命中测试 → 锁定
		if (f.justPressed)
		{
			TouchButton* hit = hitTest(f.x, f.y);
			if (hit)
			{
				m_locked[f.id] = hit;
				hit->serviceSetPressed(true);
			}
			continue;
		}

		// 持续按住：手指锁定语义（滑动不换目标），无需动作
	}
}

void TouchService::addButton(TouchButton* btn)
{
	if (!btn) return;
	Lock lock(m_mutex);
	for (auto* b : m_buttons)
		if (b == btn) return;   // 去重
	m_buttons.push_back(btn);
}

void TouchService::removeButton(TouchButton* btn)
{
	if (!btn) return;
	Lock lock(m_mutex);
	for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it)
	{
		if (*it == btn)
		{
			m_buttons.erase(it);
			break;
		}
	}
	// 若正被某手指锁定，解除绑定（不主动发松开：对象已删除）
	for (auto& kv : m_locked)
		if (kv.second == btn) kv.second = nullptr;
}

void TouchService::clearButtons()
{
	Lock lock(m_mutex);
	m_buttons.clear();
	m_locked.clear();
}

void TouchService::removeButtonsOfOwner(App* owner)
{
	if (!owner) return;

	// 先收集，释放锁后再逐个 unbind（unbind 内部拿锁，避免重入死锁）
	std::vector<TouchButton*> toRemove;
	{
		Lock lock(m_mutex);
		for (auto* b : m_buttons)
			if (b->owner() == owner) toRemove.push_back(b);
	}
	for (auto* b : toRemove)
		b->unbind();
}

// 点在矩形内判断（lv_area_is_point_on 是 LVGL private API，勿依赖）
static inline bool pointInArea(int32_t x, int32_t y, const lv_area_t* a)
{
	return x >= a->x1 && x <= a->x2 && y >= a->y1 && y <= a->y2;
}

TouchButton* TouchService::hitTest(int16_t x, int16_t y) const
{
	const lv_point_t p = { x, y };
	// 从后往前（后注册优先，近似 z-order）
	for (auto it = m_buttons.rbegin(); it != m_buttons.rend(); ++it)
	{
		TouchButton* b = *it;
		if (!b->valid()) continue;
		// owner 过滤：只命中当前前台 App 的按钮（owner 为空 = 始终分发）
		if (b->owner() && m_display && b->owner() != m_display->getActiveApp()) continue;
		if (!b->obj()) continue;
		// 隐藏对象不命中（如 GameOver 后才显示的按钮）
		if (lv_obj_has_flag(b->obj(), LV_OBJ_FLAG_HIDDEN)) continue;

		lv_area_t area;
		lv_obj_get_click_area(b->obj(), &area);
		if (pointInArea(p.x, p.y, &area))
			return b;
	}
	return nullptr;
}
