#pragma once

#include "touch/multiTouchTracker.hpp"
#include "mutex/mutex.hpp"
#include <map>
#include <vector>

class Touch;
class Display;
class TouchButton;
class App;

/**
 * @brief 触摸服务（全局单例）：多指触摸中间层
 *
 * 每帧 update()：读 GT911 全部触点 → MultiTouchTracker 逐指状态
 * → 命中注册的 TouchButton（回调跟随对象）→ 触发各按钮回调。
 * 与手柄/远程输入并行，互不冲突（触摸成为第 4 个并行输入源）。
 *
 * 生命周期：
 *   - TouchButton bind 时注册 LV_EVENT_DELETE → 对象删除自动注销（防悬垂）
 *   - owner 过滤：只分发 owner == Display::getActiveApp() 的按钮（App 切换安全）
 *   - App deinit() 可调 removeButtonsOfOwner(this) 确定性清理
 *
 * ⚠ 约定：TouchButton 回调内不得再调用 TouchService 方法（防非递归锁死锁），
 *   回调应保持轻量，只驱动游戏逻辑（如 setDirection / 玩家输入）。
 */
class TouchService
{
public:
	static constexpr int MaxFingers = MultiTouchTracker::MaxFingers;

	static TouchService& instance();

	/// 绑定 GT911 触摸与显示（main.cpp 在 Touch 初始化后调用一次）
	void init(Touch* touch, Display* display);

	/// 每帧：读多点 → tracker → 命中 → 回调（内部自行持 LVGL 锁 + 注册表锁）
	void update();

	void addButton(TouchButton* btn);
	void removeButton(TouchButton* btn);
	void clearButtons();
	void removeButtonsOfOwner(App* owner);

	/// 逐指状态（划动类游戏如 FruitNinja 查询用）
	const MultiTouchTracker& tracker() const { return m_tracker; }

private:
	TouchService() = default;

	TouchButton* hitTest(int16_t x, int16_t y) const;

	Touch* m_touch{};
	Display* m_display{};
	MultiTouchTracker m_tracker;
	std::vector<TouchButton*> m_buttons;
	std::map<int, TouchButton*> m_locked;   // track_id → 锁定按钮
	mutable Mutex m_mutex;
};
