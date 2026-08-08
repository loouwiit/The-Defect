#include "testApp.hpp"
#include "app/appStackManager.hpp"
#include "touch/gt911Indev.hpp"
#include "gui/gui.hpp"
#include "elfManager/elfManager.hpp"
#include "esp_log.h"
#include "esp_random.h"

TestApp::TestApp(Display* display, uint32_t value) :
	App(display),
	m_value(value)
{
	ESP_LOGI(TAG, "TestApp created (value=%lu)", value);
}

TestApp::~TestApp()
{
	ESP_LOGI(TAG, "TestApp deleted (value=%lu)", m_value);
}

void TestApp::init()
{
	App::init();

	if (auto guard = display->lockGuard())
	{
		lv_obj_set_style_bg_color(screen, GUI::Color::BG, 0);
		lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

		// 分屏参考线（同屏多人左/右分界）
		lv_obj_t* line = lv_obj_create(screen);
		lv_obj_remove_style_all(line);
		lv_obj_set_size(line, 2, LV_PCT(100));
		lv_obj_set_style_bg_color(line, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
		lv_obj_align(line, LV_ALIGN_LEFT_MID, 1280 / 2, 0);

		// 5 个手指标记（圆点 + 槽号:trackId 标签）
		static const lv_color_t colors[5] = {
			GUI::Color::DANGER, GUI::Color::SUCCESS,
			GUI::Color::PRIMARY, GUI::Color::WARNING,
			lv_color_hex(0xE040FB)
		};
		for (int i = 0; i < 5; i++)
		{
			auto* dot = lv_obj_create(screen);
			lv_obj_remove_style_all(dot);
			lv_obj_set_size(dot, 36, 36);
			lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
			lv_obj_set_style_bg_color(dot, colors[i], 0);
			lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(dot, 2, 0);
			lv_obj_set_style_border_color(dot, GUI::Color::TEXT, 0);
			lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);   // 默认隐藏（无手指）
			m_markers[i].dot = dot;

			auto* tag = lv_label_create(dot);
			lv_label_set_text_fmt(tag, "%d", i);
			lv_obj_set_style_text_color(tag, GUI::Color::TEXT, 0);
			lv_obj_center(tag);
			m_markers[i].tag = tag;
		}

		// 提示
		lv_obj_t* hint = lv_label_create(screen);
		lv_label_set_text(hint, "ELF: X加载 A运行 Y重载 B返回 · 多指可视化");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

		// ── ELF 测试面板（左上角卡片） ──
		lv_obj_t* panel = GUI::createCard(screen, 560, 190);
		lv_obj_set_pos(panel, 10, 10);
		lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);

		m_elfStatus = GUI::createLabel(panel, "ELF 未加载");
		lv_obj_set_style_text_color(m_elfStatus, GUI::Color::SUBTLE, 0);
		lv_obj_set_pos(m_elfStatus, 12, 8);

		const char* labels[] = { "加载", "运行", "卸载", "重载" };
		for (int i = 0; i < 4; i++)
		{
			auto* b = GUI::createButton(panel, labels[i], 116, 42);
			lv_obj_set_user_data(b, (void*)(intptr_t)i);
			lv_obj_add_event_cb(b, onElfBtnCb, LV_EVENT_CLICKED, this);
			lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 12 + i * 126, -12);
		}
		refreshElfPanel();
	}

	// 60fps 刷新任务（轮询 GT911Indev 槽位 → 更新圆点）
	m_tick = Task::addTask(tickCb, "testTouch", this, 16, Task::Affinity::None);
}

void TestApp::deinit()
{
	if (m_tick)
	{
		Task::removeTask(m_tick);
		m_tick = nullptr;
	}
	App::deinit();
}

void TestApp::onForeground()
{
	nextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
	refreshElfPanel();   // 从 ELF App 返回时刷新 refcount/dlclose 结果
}

TickType_t TestApp::tickCb(void* param)
{
	static_cast<TestApp*>(param)->refresh();
	return 16;   // ~60fps
}

void TestApp::refresh()
{
	if (!running) return;

	if (auto guard = display->lockGuard())
	{
		GT911Indev::SlotInfo slots[5];
		GT911Indev::instance().getSlots(slots, 5);

		for (int i = 0; i < 5; i++)
		{
			lv_obj_t* dot = m_markers[i].dot;
			const auto& info = slots[i];
			if (info.active)
			{
				lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
				lv_obj_set_pos(dot, info.point.x - 18, info.point.y - 18);
				lv_obj_set_style_border_width(dot, info.pressed ? 4 : 1, 0);
				lv_label_set_text_fmt(m_markers[i].tag, "%d:%d%s", i, info.trackId, info.pressed ? "" : "R");
			}
			else
			{
				lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════
// ELF 测试面板
// ════════════════════════════════════════════════════════════════

void TestApp::elfAction(int action)
{
	switch (action)
	{
	case 0: // 加载
		ElfManager::instance().ensureLoaded(SMOKE_PATH);
		break;

	case 1: // 运行
		{
			auto* app = ElfManager::instance().createApp(SMOKE_PATH, display, nullptr);
			if (app)
			{
				ESP_LOGI(TAG, "运行 ELF App: %p", (void*)app);
				pushApp(app);
			}
			else
				ESP_LOGE(TAG, "createApp 失败（未加载/符号错误）");
		}
		break;

	case 2: // 卸载
		ElfManager::instance().unload(SMOKE_PATH);
		break;

	case 3: // 重载（强制）
		ElfManager::instance().unload(SMOKE_PATH);
		ElfManager::instance().ensureLoaded(SMOKE_PATH);
		break;

	default:
		break;
	}
}

void TestApp::refreshElfPanel()
{
	if (!m_elfStatus) return;

	auto& em = ElfManager::instance();
	char buf[160];
	if (em.isLoaded(SMOKE_PATH))
	{
		snprintf(buf, sizeof(buf),
			"module=%s v%lu ref=%lu load=%lums size=%u",
			em.name(SMOKE_PATH), (unsigned long)em.version(SMOKE_PATH),
			(unsigned long)em.refcount(SMOKE_PATH),
			(unsigned long)em.loadTimeMs(SMOKE_PATH), (unsigned)em.moduleSize(SMOKE_PATH));
	}
	else
	{
		snprintf(buf, sizeof(buf), "未加载  %s", SMOKE_PATH);
	}

	if (auto guard = display->lockGuard())
	{
		lv_label_set_text(m_elfStatus, buf);
		lv_obj_set_style_text_color(m_elfStatus,
			em.isLoaded(SMOKE_PATH) ? GUI::Color::SUCCESS : GUI::Color::SUBTLE, 0);
	}
}

void TestApp::onElfBtnCb(lv_event_t* e)
{
	auto* self = static_cast<TestApp*>(lv_event_get_user_data(e));
	self->m_pendingAction = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));

	// LVGL 事件回调中不可做栈操作/阻塞加载 → Task::addTask 延后
	Task::addTask([](void* p) -> TickType_t
		{
			auto* t = static_cast<TestApp*>(p);
			t->elfAction(t->m_pendingAction);
			t->refreshElfPanel();
			return Task::infinityTime;
		}, "elfAction", self, 0, Task::Affinity::None);
}

// ════════════════════════════════════════════════════════════════
// BLE 手柄输入
// ════════════════════════════════════════════════════════════════

void TestApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	if (nextAppChangeTime >= xTaskGetTickCount())
		return;
	nextAppChangeTime = xTaskGetTickCount() + 300; // 防抖

	if (state.isPressed(GamepadButton::BTN_X))
	{
		ESP_LOGI(TAG, "X — 加载");
		elfAction(0);
		refreshElfPanel();
	}

	if (state.isPressed(GamepadButton::BTN_A))
	{
		ESP_LOGI(TAG, "A — 运行");
		elfAction(1);
	}

	if (state.isPressed(GamepadButton::BTN_Y))
	{
		ESP_LOGI(TAG, "Y — 重载");
		elfAction(3);
		refreshElfPanel();
	}

	if (state.isPressed(GamepadButton::BTN_B))
	{
		ESP_LOGI(TAG, "B — 返回");
		popApp();
	}
}
