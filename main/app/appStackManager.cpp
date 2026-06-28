#include "app/appStackManager.hpp"
#include <algorithm>
#include <esp_log.h>

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

AppStackManager::AppStackManager(Display* display)
	: m_display(display)
{
}

AppStackManager::~AppStackManager()
{
	// 逆序销毁所有栈（后台栈先销毁）
	for (auto it = m_stacks.rbegin(); it != m_stacks.rend(); ++it)
		delete *it;
	m_stacks.clear();
	m_activeStack = nullptr;
}

// ════════════════════════════════════════════════════════════════
// 多栈管理
// ════════════════════════════════════════════════════════════════

AppStack* AppStackManager::createStack()
{
	auto* stack = new AppStack(m_display, this);
	m_stacks.push_back(stack);

	// 第一个创建的栈自动设为 active
	if (!m_activeStack)
		m_activeStack = stack;

	ESP_LOGI(TAG, "createStack: total=%zu, active=%p", m_stacks.size(), m_activeStack);
	return stack;
}

void AppStackManager::destroyStack(AppStack* stack)
{
	if (!stack) return;

	// 不允许销毁 active 栈
	if (stack == m_activeStack)
	{
		ESP_LOGW(TAG, "destroyStack: cannot destroy active stack, switch first");
		return;
	}

	auto it = std::find(m_stacks.begin(), m_stacks.end(), stack);
	if (it == m_stacks.end())
	{
		ESP_LOGW(TAG, "destroyStack: stack not found");
		return;
	}

	m_stacks.erase(it);
	delete stack;
	ESP_LOGI(TAG, "destroyStack: total=%zu", m_stacks.size());
}

bool AppStackManager::switchToStack(size_t index)
{
	if (index >= m_stacks.size())
	{
		ESP_LOGW(TAG, "switchToStack: index %zu out of range (total=%zu)", index, m_stacks.size());
		return false;
	}
	return switchToStack(m_stacks[index]);
}

bool AppStackManager::switchToStack(AppStack* stack)
{
	if (!stack)
	{
		ESP_LOGW(TAG, "switchToStack: null stack");
		return false;
	}

	if (stack == m_activeStack)
		return true;  // 已经在目标栈，无操作

	// 检查 stack 是否在管理列表中
	auto it = std::find(m_stacks.begin(), m_stacks.end(), stack);
	if (it == m_stacks.end())
	{
		ESP_LOGW(TAG, "switchToStack: stack not managed");
		return false;
	}

	transitionStacks(m_activeStack, stack);
	m_activeStack = stack;

	ESP_LOGI(TAG, "switchToStack: active=%p (depth=%zu)", m_activeStack, m_activeStack->depth());
	return true;
}

// ════════════════════════════════════════════════════════════════
// 栈操作（委托到 activeStack）
// ════════════════════════════════════════════════════════════════

void AppStackManager::push(App* app)
{
	if (!m_activeStack)
	{
		ESP_LOGE(TAG, "push: no active stack");
		return;
	}
	m_activeStack->push(app);
}

void AppStackManager::pop()
{
	if (!m_activeStack)
	{
		ESP_LOGE(TAG, "pop: no active stack");
		return;
	}

	// 保护：根栈 depth ≤ 1 时禁止 pop（防止桌面被弹出，导致无 screen 可显示）
	if (!m_stacks.empty() && m_activeStack == m_stacks[0] && m_activeStack->depth() <= 1)
	{
		ESP_LOGW(TAG, "pop: at root stack, cannot pop the last app");
		return;
	}

	// pop 返回 orphan 表示栈空了需要调用方处理删除
	App* orphan = m_activeStack->pop();

	// 如果 active 栈已空，且不是根栈 → 自动切回根栈 + 销毁空栈
	if (m_activeStack->isEmpty() && m_stacks.size() > 1 && m_activeStack != m_stacks[0])
	{
		auto* emptyStack = m_activeStack;
		ESP_LOGI(TAG, "pop: active stack empty, switching to root stack");

		// 先切屏（加载 DesktopApp screen），再异步删 orphan
		switchToStack(m_stacks[0]);
		if (orphan)
			AppStack::scheduleDeletion(orphan);

		// scheduleDeletion 是静态方法，Task 不依赖 AppStack，可以安全立即销毁
		destroyStack(emptyStack);
	}
}

void AppStackManager::replace(App* app)
{
	if (!m_activeStack)
	{
		ESP_LOGE(TAG, "replace: no active stack");
		return;
	}
	m_activeStack->replace(app);
}

// ════════════════════════════════════════════════════════════════
// 新建栈 + 推入首 app（自动切换）
// ════════════════════════════════════════════════════════════════

AppStack* AppStackManager::pushToNewStack(App* app)
{
	auto* oldStack = m_activeStack;

	// 旧栈顶退到后台
	if (oldStack && !oldStack->isEmpty())
		oldStack->top()->onBackground();

	// 创建新栈并切为 active
	auto* newStack = new AppStack(m_display, this);
	m_stacks.push_back(newStack);
	m_activeStack = newStack;

	// 推入第一个 app（内部会 init + applyApp + onForeground）
	newStack->push(app);

	ESP_LOGI(TAG, "pushToNewStack: new stack=%p, total=%zu", newStack, m_stacks.size());
	return newStack;
}

// ════════════════════════════════════════════════════════════════
// 输入路由
// ════════════════════════════════════════════════════════════════

App* AppStackManager::activeApp() const
{
	return m_activeStack ? m_activeStack->top() : nullptr;
}

// ════════════════════════════════════════════════════════════════
// 内部 — 栈切换
// ════════════════════════════════════════════════════════════════

void AppStackManager::transitionStacks(AppStack* from, AppStack* to)
{
	// 旧栈顶退到后台
	if (from && !from->isEmpty())
		from->top()->onBackground();

	// 新栈顶进入前台
	if (to && !to->isEmpty())
	{
		m_display->applyApp(to->top());
		to->top()->onForeground();
	}
	else
	{
		ESP_LOGW(TAG, "transitionStacks: target stack is empty");
	}
}