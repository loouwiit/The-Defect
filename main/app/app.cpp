#include "app.hpp"
#include "display/font.hpp"
#include "app/appStackManager.hpp"

App::App(Display* display) :
	display{ display },
	screen{ nullptr }
{
	// ⚠ 构造可能发生在任意线程（app_main / BLE task / Task daemon / deleteApp task），
	//   而 LVGL worker 此时并发渲染 → 创建/修改对象树必须持 LVGL 锁（本仓库锁纪律）。
	//   原实现直接在初始化列表 lv_obj_create 裸操作，是随机 UI 崩溃的潜在来源。
	if (auto guard = display->lockGuard())
	{
		screen = lv_obj_create(nullptr);
		lv_obj_set_size(screen, lv_display_get_vertical_resolution(display->getLvglDisplay()), lv_display_get_horizontal_resolution(display->getLvglDisplay()));
		lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
		lv_obj_set_scroll_dir(screen, LV_DIR_NONE);
		lv_obj_set_style_text_font(screen, FontLoader::getDefault(), 0);
	}
}

App::~App()
{
	if (running)
		deinit();

	while (!deletable)
		vTaskDelay(10);

	if (auto guard = display->lockGuard())
		lv_obj_delete(screen);
}

// ── 便利方法（委托给 AppStackManager） ──

void App::pushApp(App* app)
{
	if (m_manager)
		m_manager->push(app);
	else
		ESP_LOGE(TAG, "pushApp: no manager set");
}

void App::popApp()
{
	if (m_manager)
		m_manager->pop();
	else
		ESP_LOGE(TAG, "popApp: no manager set");
}

void App::replaceWith(App* app)
{
	if (m_manager)
		m_manager->replace(app);
	else
		ESP_LOGE(TAG, "replaceWith: no manager set");
}

void App::setManager(AppStackManager* manager)
{
	m_manager = manager;
}
