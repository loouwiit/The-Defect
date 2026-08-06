#include "touch/touchButton.hpp"
#include "touch/touchService.hpp"
#include <utility>

void TouchButton::bind(lv_obj_t* obj, App* owner, Cb cb, void* user)
{
	unbind();

	m_obj = obj;
	m_cb = std::move(cb);
	m_useLvgl = false;
	m_user = user;
	m_owner = owner;
	m_valid = true;
	m_pressed = false;

	// 对象被删除时自动从 TouchService 注销（防悬垂）
	lv_obj_add_event_cb(obj, onDeleteCb, LV_EVENT_DELETE, this);

	TouchService::instance().addButton(this);
}

void TouchButton::bindLvgl(lv_obj_t* obj, App* owner)
{
	unbind();

	m_obj = obj;
	m_cb = nullptr;
	m_useLvgl = true;
	m_user = nullptr;
	m_owner = owner;
	m_valid = true;
	m_pressed = false;

	lv_obj_add_event_cb(obj, onDeleteCb, LV_EVENT_DELETE, this);

	TouchService::instance().addButton(this);
}

void TouchButton::unbind()
{
	if (!m_valid) return;
	TouchService::instance().removeButton(this);
	m_valid = false;
	m_obj = nullptr;
	m_cb = nullptr;
	m_pressed = false;
}

void TouchButton::onDeleteCb(lv_event_t* e)
{
	auto* self = static_cast<TouchButton*>(lv_event_get_user_data(e));
	if (!self) return;
	self->serviceMarkDeleted();
	TouchService::instance().removeButton(self);
}

void TouchButton::serviceMarkDeleted()
{
	if (!m_valid) return;
	m_valid = false;
	m_obj = nullptr;
	if (m_pressed)
	{
		m_pressed = false;
		// 对象正在删除：自定义回调仍可通知松开；LVGL 兼容模式不能再 send_event
		if (m_cb) m_cb(false, m_user);
	}
}

void TouchButton::serviceSetPressed(bool pressed)
{
	if (pressed == m_pressed) return;
	m_pressed = pressed;

	if (m_cb)
	{
		m_cb(pressed, m_user);
	}
	else if (m_useLvgl && m_obj)
	{
		if (pressed)
			lv_obj_send_event(m_obj, LV_EVENT_PRESSED, nullptr);
		else
		{
			lv_obj_send_event(m_obj, LV_EVENT_RELEASED, nullptr);
			lv_obj_send_event(m_obj, LV_EVENT_CLICKED, nullptr);
		}
	}
}
