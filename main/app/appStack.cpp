#include "app/appStack.hpp"
#include "app/appStackManager.hpp"
#include "task/task.hpp"
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
		(*it)->deinit();
		while (!(*it)->deletable) vTaskDelay(10);
		delete *it;
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

			oldApp->deinit();

			while (!oldApp->deletable)
				vTaskDelay(10);

			if (auto guard = oldApp->display->lockGuard())
			{
				delete oldApp;
			}
			else
			{
				ESP_LOGE(TAG, "scheduleDeletion: failed to lock display, retrying");
				return TickType_t(10);
			}

			return Task::infinityTime;
		}, "deleteApp", app, 0, Task::Affinity::None);
}
