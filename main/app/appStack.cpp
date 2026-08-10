#include "app/appStack.hpp"
#include "app/appStackManager.hpp"
#include "task/task.hpp"
#include "elfManager/elfManager.hpp"
#include "gamepad/virtualGamepad.hpp"
#include <esp_log.h>

static constexpr char TAG[] = "AppStack";

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

AppStack::AppStack(Display* display, AppStackManager* manager)
	: m_display(display)
	, m_manager(manager)
{
}

AppStack::~AppStack()
{
	// 清理仍在栈内的 app（非空栈被销毁时）
	for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it)
	{
		char elfPath[64];
		snprintf(elfPath, sizeof(elfPath), "%s", (*it)->m_elfModulePath);   // delete 前取（delete 后对象已释放）
		(*it)->deinit();
		while (!(*it)->deletable) vTaskDelay(10);
		delete* it;
		if (elfPath[0])
			ElfManager::instance().onAppDestroyed(elfPath);
	}
	m_stack.clear();
}

// ════════════════════════════════════════════════════════════════
// 查询
// ════════════════════════════════════════════════════════════════

App* AppStack::top() const
{
	return m_stack.empty() ? nullptr : m_stack.back();
}

// ════════════════════════════════════════════════════════════════
// 栈操作
// ════════════════════════════════════════════════════════════════

void AppStack::push(App* app)
{
	auto* oldTop = top();

	if (oldTop)
	{
		ESP_LOGD(TAG, "push: background old top %p", oldTop);
		oldTop->onBackground();
	}

	app->setManager(m_manager);
	if (!app->running)
		app->init();

	m_stack.push_back(app);
	m_display->applyApp(app);
	app->onForeground();

	ESP_LOGI(TAG, "push: stack depth=%zu", m_stack.size());
}

App* AppStack::pop()
{
	if (m_stack.empty())
	{
		ESP_LOGW(TAG, "pop: stack is empty");
		return nullptr;
	}

	auto* oldTop = top();
	m_stack.pop_back();
	oldTop->onBackground();

	auto* newTop = top();
	if (newTop)
	{
		// 先加载新 screen，再异步删旧的 — LVGL 始终有有效 screen
		m_display->applyApp(newTop);
		newTop->onForeground();
		scheduleDeletion(oldTop);
		ESP_LOGI(TAG, "pop: stack depth=%zu", m_stack.size());
		return nullptr;
	}
	else
	{
		ESP_LOGW(TAG, "pop: stack became empty");
		ESP_LOGI(TAG, "pop: stack depth=0");
		// 返回给调用方：须先切屏再调 scheduleDeletion
		return oldTop;
	}
}

void AppStack::replace(App* app)
{
	if (m_stack.empty())
	{
		ESP_LOGW(TAG, "replace: stack is empty, using push");
		push(app);
		return;
	}

	auto* oldTop = top();
	m_stack.back() = app;

	// ⚠ push/pop 均调用 oldTop->onBackground()，replace 此前遗漏：
	//   被替换 App 的后台定时器/任务不停（如 LuaGameApp::onBackground 置 m_paused 不生效，
	//   游戏循环在 deinit 前继续跑），前台状态机错乱。
	oldTop->onBackground();

	app->setManager(m_manager);
	app->init();

	m_display->applyApp(app);
	app->onForeground();

	ESP_LOGI(TAG, "replace: stack depth=%zu", m_stack.size());

	scheduleDeletion(oldTop);
}

// ════════════════════════════════════════════════════════════════
// 内部 — 独立 Task 异步删除
// ════════════════════════════════════════════════════════════════

void AppStack::scheduleDeletion(App* app)
{
	// 每个旧 app 独立 Task，自管理 deinit + 等待 + 删除
	// 无需队列、无需顺序、不依赖 AppStack
	Task::addTask([](void* param) -> TickType_t
		{
			auto* oldApp = (App*)param;

			if (oldApp->running)
				oldApp->deinit();

			if (!oldApp->deletable)
				return 10;

			// ⚠ 防 UAF：先等 VirtualGamepad 分发空闲（dispatchTask 当前帧 handler 全部返回）
			//   再拿 LVGL 锁 delete——否则 dispatchTask 可能在已删除 App 上调用
			//   onGamepadInput 虚函数（崩溃根因，vGamepadDisp 悬空指针）。
			//   锁序：waitDispatchIdle（拿即放，不持锁等待）→ LVGL，与 dispatchTask
			//   （dispatchLock → LVGL）一致，无 ABBA 死锁。
			VirtualGamepad::instance().waitDispatchIdle();

			// delete 前取 ELF 模块路径（delete 后对象已释放，不能读成员）
			char elfPath[64];
			snprintf(elfPath, sizeof(elfPath), "%s", oldApp->m_elfModulePath);

			if (auto guard = oldApp->display->lockGuard())
			{
				delete oldApp;
			}
			else
			{
				ESP_LOGE(TAG, "scheduleDeletion: failed to lock display, retrying");
				return TickType_t(10);
			}

			// delete 完成后：ELF 模块 refcount--，归 0 时 dlclose
			if (elfPath[0])
				ElfManager::instance().onAppDestroyed(elfPath);

			return Task::infinityTime;
		}, "deleteApp", app, 0, Task::Affinity::None);
}
