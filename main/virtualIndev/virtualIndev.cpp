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

	// ⚠ 必须赋值成员 `this->indev`：原实现 `lv_indev_t* indev = lv_indev_create()` 是
	//   局部变量遮蔽成员 → 成员恒为 nullptr → 幂等守卫永不成立，重复 start() 会重复注册
	//   indev（泄漏 + 双输入源）。
	this->indev = lv_indev_create();

	if (!indev)
	{
		ESP_LOGE(TAG, "failed to register virtual indev");
		return false;
	}

	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, indevReadCb);
	// 绑定到当前 display（与 GT911Indev 一致，多 display 时避免默认 display 错配）
	if (display->getLvglDisplay())
		lv_indev_set_display(indev, display->getLvglDisplay());

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
		// fallback: 锁被占用（WS 写线程瞬时持有）时返回**上一采样**（不清标志）——
		// 原返回 (0,0)+REL 的失败模式恶劣：正在按压的手指被强制提前 RE（丢按压），
		// 且 REL 态下 LVGL 会把 (0,0) 与上次坐标间的移动解释为一次点击左上角
		// （屏幕左上恰是返回/关闭按钮区）。写侧仅 5 次赋值，窗口极小，
		// 下帧即可正常读取。
		data->state = self.sample.state;
		data->point = self.sample.point;
		data->continue_reading = false;
	}
}
