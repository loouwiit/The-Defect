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

	// ── 栈操作（由 AppStackManager 调用） ──
	void push(App* app);
	/** @brief 弹出栈顶
	 *  @return 当栈变空时返回旧 app（调用方须先切屏再调 scheduleDeletion），否则已内部处理并返回 nullptr */
	App* pop();
	void replace(App* app);

	// ── 内部 ──
	static void scheduleDeletion(App* app);      // 独立 Task：deinit → 等 deletable → lockGuard → delete

	Display* m_display;
	AppStackManager* m_manager;

	std::vector<App*> m_stack;

	static constexpr char TAG[] = "AppStack";
};
