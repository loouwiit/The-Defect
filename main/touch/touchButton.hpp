#pragma once

#include "lvgl.h"
#include <functional>

class TouchService;
class App;

/**
 * @brief 触摸按钮（回调跟随对象，仿 lv_obj_add_event_cb 模型）
 *
 * 绑定一个 LVGL 对象作为命中区域，由 TouchService 每帧命中分发。
 * 两种回调模式：
 *   - bind()    自定义回调 onPressed(bool pressed)，按住持续，回调跟随对象
 *   - bindLvgl() LVGL 兼容：命中/松开 → lv_obj_send_event(obj, LV_EVENT_PRESSED/RELEASED/CLICKED)
 * 生命周期：绑定对象被删除时（LV_EVENT_DELETE）自动从 TouchService 注销；
 *          也可显式 unbind()。
 */
class TouchButton
{
public:
	using Cb = std::function<void(bool pressed, void* user)>;

	TouchButton() = default;
	~TouchButton() { unbind(); }

	TouchButton(const TouchButton&) = delete;
	TouchButton& operator=(const TouchButton&) = delete;

	/// 自定义回调模式（按住持续），回调跟随对象
	void bind(lv_obj_t* obj, App* owner, Cb cb, void* user = nullptr);
	/// LVGL 兼容模式：命中/松开 → lv_obj_send_event(obj, LV_EVENT_*)，复用现有 LVGL 回调
	void bindLvgl(lv_obj_t* obj, App* owner);

	/// 手动解绑（从 TouchService 注销）
	void unbind();

	bool valid() const { return m_valid; }
	bool pressed() const { return m_pressed; }
	void* owner() const { return m_owner; }
	lv_obj_t* obj() const { return m_obj; }

	// —— TouchService 内部接口 ——
	void serviceSetPressed(bool pressed);   // 命中/松开时由 TouchService 调用
	void serviceMarkDeleted();              // LV_EVENT_DELETE 时标记失效

private:
	static void onDeleteCb(lv_event_t* e);

	lv_obj_t* m_obj{};
	Cb m_cb;
	App* m_owner{};
	void* m_user{};

	bool m_useLvgl = false;
	bool m_valid = false;
	bool m_pressed = false;
};
