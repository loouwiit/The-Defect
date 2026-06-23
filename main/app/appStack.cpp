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
	// 从栈顶向下 deinit（让子 app 先退出）
	for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it)
		(*it)->deinit();

	// 同步删除所有 app
	for (auto* app : m_stack)
		delete app;
	m_stack.clear();

	// 清理尚未删除的待删除队列
	for (auto* app : m_pendingDeletion)
		delete app;
	m_pendingDeletion.clear();
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

	// 旧 app 退到后台
	if (oldTop)
	{
		ESP_LOGD(TAG, "push: background old top %p", oldTop);
		oldTop->onBackground();
	}

	// 新 app 关联管理器
	app->setManager(m_manager);

	// 初始化（创建 LVGL 对象等）
	app->init();

	// 入栈 + 加载 screen + 前台通知
	m_stack.push_back(app);
	m_display->applyApp(app);
	app->onForeground();

	ESP_LOGI(TAG, "push: stack depth=%zu", m_stack.size());

	// push 不删旧 app — 它在栈底，等 pop 时才删
}

void AppStack::pop()
{
	if (m_stack.empty())
	{
		ESP_LOGW(TAG, "pop: stack is empty");
		return;
	}

	auto* oldTop = top();
	m_stack.pop_back();
	oldTop->onBackground();

	auto* newTop = top();
	if (newTop)
	{
		// 恢复上一个 app 的 screen（不修改栈，仅切换显示）
		m_display->applyApp(newTop);
		newTop->onForeground();
	}
	else
	{
		ESP_LOGW(TAG, "pop: stack became empty");
	}

	ESP_LOGI(TAG, "pop: stack depth=%zu", m_stack.size());

	scheduleDeletion(oldTop);
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

	// 替换栈顶（不触发 oldTop 的 onBackground，因为它即将销毁）
	m_stack.back() = app;

	// 新 app 关联管理器
	app->setManager(m_manager);

	// 初始化
	app->init();

	// 加载 screen + 前台通知（栈指针已更新，不需要再次 push_back）
	m_display->applyApp(app);
	app->onForeground();

	ESP_LOGI(TAG, "replace: stack depth=%zu", m_stack.size());

	// 旧 app 直接销毁
	scheduleDeletion(oldTop);
}

// ════════════════════════════════════════════════════════════════
// 内部 — 异步删除
// ════════════════════════════════════════════════════════════════

void AppStack::scheduleDeletion(App* app)
{
	app->deinit();

	// 推入待删除队列
	bool wasEmpty = m_pendingDeletion.empty();
	m_pendingDeletion.push_back(app);

	// 仅当没有轮询 Task 在跑时才创建新的
	if (!wasEmpty)
		return;

	Task::addTask([](void* param) -> TickType_t
		{
			auto& self = *(AppStack*)param;

			for (auto it = self.m_pendingDeletion.begin(); it != self.m_pendingDeletion.end(); )
			{
				auto* oldApp = *it;
				if (oldApp->deletable)
				{
					ESP_LOGD(TAG, "deleting old app %p", oldApp);
					// delete 会触发 ~App() → lv_obj_delete(screen)，必须持 LVGL 锁
					if (auto guard = self.m_display->lockGuard())
					{
						delete oldApp;
					}
					else
					{
						ESP_LOGE(TAG, "deleteApp: failed to lock display, retrying");
						return TickType_t(10);
					}
					it = self.m_pendingDeletion.erase(it);
				}
				else
				{
					++it;
				}
			}

			// 还有待删除则继续轮询，否则退出
			return self.m_pendingDeletion.empty()
				? Task::infinityTime
				: TickType_t(10);
		}, "deleteApp", this, 0, Task::Affinity::None);
}
