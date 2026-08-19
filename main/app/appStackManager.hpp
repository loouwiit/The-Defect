#pragma once

#include <vector>
#include <cstddef>
#include "app/appStack.hpp"
#include "mutex/mutex.hpp"

/**
 * @brief 多栈管理器
 *
 * 管理多个 AppStack，支持多调用栈并行存活。
 * - 仅 activeStack 的 top app 显示在前台
 * - 切换栈时自动触发 onBackground / onForeground
 * - push / pop / replace 委托到 activeStack
 *
 * 用法:
 * @code
 * AppStackManager mgr(&display);
 * auto* root = mgr.createStack();
 * root->push(new DesktopApp(&display));
 *
 * // 从桌面打开游戏 → 自动新栈
 * mgr.pushToNewStack(new GameApp(&display));
 *
 * // 切回桌面
 * mgr.switchToStack(0);
 * @endcode
 */
class AppStackManager
{
public:
	static AppStackManager& instance();   // Meyer's Singleton（magic static，仅构造一次 + 线程安全）

	void init(Display* display);          // 注入 Display（main.cpp 单线程阶段调用一次）

	AppStackManager(AppStackManager& copy) = delete;
	AppStackManager& operator=(AppStackManager& copy) = delete;
	// 不可移动：成员 RecursiveMutex 无移动语义，隐式移动会按值拷贝句柄 → 悬空锁
	AppStackManager(AppStackManager&& move) = delete;
	AppStackManager& operator=(AppStackManager&& move) = delete;

	// ── 多栈管理 ──
	AppStack* createStack();                    // 新建空栈
	void destroyStack(AppStack* stack);          // 销毁栈（删除所有 app）
	bool switchToStack(size_t index);            // 按索引切换前台栈
	bool switchToStack(AppStack* stack);         // 按指针切换
	AppStack* activeStack() const { return m_activeStack; }
	size_t stackCount() const { return m_stacks.size(); }

	// ── 栈操作（委托到 activeStack） ──
	void push(App* app);
	void pop();
	void replace(App* app);

	// ── 新建栈并推入第一个 app（用于"从桌面打开新游戏→新栈"） ──
	AppStack* pushToNewStack(App* app);

	// ── 输入路由 ──
	App* activeApp() const;

private:
	AppStackManager() = default;
	~AppStackManager();   // 单例从不释放（程序运行期常驻，此析构仅满足编译器）

	void transitionStacks(AppStack* from, AppStack* to);

	Display* m_display{};
	std::vector<AppStack*> m_stacks;
	AppStack* m_activeStack{};
	RecursiveMutex m_mutex;

	static constexpr char TAG[] = "AppStackManager";
};
