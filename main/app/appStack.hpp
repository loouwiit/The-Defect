#pragma once

#include <vector>
#include <cstddef>
#include "app/app.hpp"
#include "mutex/mutex.hpp"

class AppStackManager;

/**
 * @brief 单调用栈
 *
 * 管理一个 App 的 LIFO 栈，提供 push / pop / replace 操作。
 * 只能由 AppStackManager 创建和管理，不对外暴露构造/析构。
 *
 * 每个 App 拥有独立 LVGL screen，栈操作自动切换显示 + 生命周期。
 */
class AppStack
{
	friend class AppStackManager;

public:
	// ── 查询 ──
	App* top() const;
	size_t depth() const { return m_stack.size(); }
	bool isEmpty() const { return m_stack.empty(); }

private:
	// 只能由 AppStackManager 创建
	AppStack(Display* display, AppStackManager* manager);
	~AppStack();

	// ── 栈操作（由 AppStackManager 调用，外部已持锁） ──
	void push(App* app);
	void pop();
	void replace(App* app);

	// ── 内部 ──
	void transitionTo(App* app);                 // 加载 screen + 生命周期
	void scheduleDeletion(App* app);             // 异步删除旧 app

	Display* m_display;
	AppStackManager* m_manager;

	std::vector<App*> m_stack;
	std::vector<App*> m_pendingDeletion;         // 等待异步删除的旧 app

	static constexpr char TAG[] = "AppStack";
};
