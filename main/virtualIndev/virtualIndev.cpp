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
	self.sample.point = point;
	self.sample.state = state;
	self.sample.updated = true;
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

	bool have = false;
	if (self.mutex.try_lock())
	{
		have = self.sample.updated;
		if (have)
		{
			data->state = self.sample.state;
			data->point = self.sample.point;
			// keep updated true until a release is delivered
			if (self.sample.state == LV_INDEV_STATE_REL)
				self.sample.updated = false;
		}
		self.mutex.unlock();
	}
	else
	{
		// fallback: if unable to lock, return no touch
		have = self.sample.updated;
	}

	if (!have)
	{
		data->state = LV_INDEV_STATE_REL;
		data->point.x = 0;
		data->point.y = 0;
	}
}
