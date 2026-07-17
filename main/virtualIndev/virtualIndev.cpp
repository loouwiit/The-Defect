#include "virtualIndev.hpp"
#include "esp_log.h"

static const char* TAG = "VirtualIndev";

VirtualIndev* VirtualIndev::s_instance = nullptr;

VirtualIndev& VirtualIndev::instance()
{
	static VirtualIndev inst;
	return inst;
}

bool VirtualIndev::start(Display* disp)
{
	if (indev) return true;
	if (!disp) return false;

	display = disp;
	s_instance = this;

	lv_indev_t* indev = lv_indev_create();

	if (!indev)
	{
		ESP_LOGE(TAG, "failed to register virtual indev");
		return false;
	}

	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, indevReadCb);

	ESP_LOGI(TAG, "virtual indev registered");
	return true;
}

void VirtualIndev::sendTouch(lv_indev_state_t state, lv_point_t point)
{
	if (!s_instance) return;
	auto& self = *s_instance;

	// 服务端去重：坐标和状态都没变，且上条数据尚未被消费
	if (self.sample.point.x == point.x &&
		self.sample.point.y == point.y &&
		self.sample.state == state &&
		self.sample.hasNewData)
	{
		return;
	}

	self.sample.point = point;
	self.sample.state = state;
	self.sample.hasNewData = true;
}

void VirtualIndev::indevReadCb(lv_indev_t* /*drv*/, lv_indev_data_t* data)
{
	if (!s_instance)
	{
		data->state = LV_INDEV_STATE_REL;
		data->point.x = 0;
		data->point.y = 0;
		return;
	}

	auto& self = *s_instance;

	if (self.mutex.try_lock())
	{
		if (self.sample.hasNewData)
		{
			// 有新事件：返回数据，清标志，让 LVGL 立即重读
			data->state = self.sample.state;
			data->point = self.sample.point;
			data->continue_reading = true;
			self.sample.hasNewData = false;
		}
		else
		{
			// 无新事件：维持当前按压状态，不强制重读
			data->state = self.sample.state;
			data->point = self.sample.point;
			data->continue_reading = false;
		}
		self.mutex.unlock();
	}
	else
	{
		// fallback: 锁被占用时返回无触摸
		data->state = LV_INDEV_STATE_REL;
		data->point.x = 0;
		data->point.y = 0;
	}
}
