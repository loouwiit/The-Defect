#pragma once

#include "esp_lv_adapter_display.h"
#include "bleGamepad/gamepadState.hpp"
#include <string>
#include <esp_log.h>
class Display;
class AppStackManager;
struct AiAction;   // 前向声明，完整定义在 aiAgent/aiProtocol.hpp

/**
 * @brief AI select 动作的目标
 */
struct AiSelectTarget {
    std::string group;   // 焦点组名（如 "cards"、"buttons"），空=当前组
    int index{};         // 组内索引，-1=不改变
};

/**
 * @brief Base application class
 *
 * Represents an "app" that can be applied to the display. Manages an LVGL screen object and provides an interface for initialization and cleanup. Derived classes can implement specific UI and behavior.
 *
 * Init must be called to set the app as running, and deinit must be called to mark it as deletable. The destructor waits until the app is marked deletable before deleting the LVGL screen object.
 * In the initialization phase, the app can doing time consuming tasks, and in the cleanup phase, it can stop those tasks.
 *
 * Please ensure the constructor is simple for fast construction. If there are complex steps, please move them to init.
 */
class App
{
public:
	App(Display* display);
	virtual ~App();

	virtual void init() { running = true; deletable = false; };
	virtual void deinit() { running = false; deletable = true; };

	// BLE 手柄输入 — 由 BleGamepad 路由到当前活跃 App
	virtual void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) {}
	virtual void onGamepadConnected(uint8_t playerId) { ESP_LOGI(TAG, "Player %d connected", playerId); }
	virtual void onGamepadDisconnected(uint8_t playerId) { ESP_LOGI(TAG, "Player %d disconnected", playerId); }

	// ── AI 能力（可选覆盖，默认空实现） ──
	/** @brief 返回本 App 的上下文描述，拼入 AI system prompt 的 {app_context} */
	virtual std::string getAiContext() { return ""; }
	/** @brief 执行 AI 发来的 app_custom 动作，true=已处理，false=交系统执行器兜底 */
	virtual bool executeAiAction(const AiAction& action) { (void)action; return false; }
	/** @brief AI select：聚焦到目标的组和索引 */
	virtual void onAiSelect(const AiSelectTarget& target) { (void)target; }
	/** @brief AI activate：触发当前聚焦的元素 */
	virtual void onAiActivate() {}

	// ── 栈生命周期钩子（由 AppStackManager 调用） ──
	/** @brief App 成为前台活跃（已 visible） */
	virtual void onForeground() {}
	/** @brief App 被覆盖/移出前台（即将 invisible） */
	virtual void onBackground() {}

	// ── 便利方法（委托给 AppStackManager） ──
	void pushApp(App* app);              // push 到当前栈
	void popApp();                       // 弹出当前栈顶
	void replaceWith(App* app);          // 替换当前栈顶

	/** @brief 设置所属管理器（由 AppStackManager/AppStack 自动调用） */
	void setManager(AppStackManager* manager);
	AppStackManager* getManager() const { return m_manager; }
	Display* getDisplay() const { return display; }

protected:
	Display* display{};
	lv_obj_t* screen{};

	bool running{};
	bool deletable{ true };

	AppStackManager* m_manager{};

	friend class Display;
	friend class AppStack;
	friend class AppStackManager;

private:
	constexpr static char TAG[] = "App";
};

#include "display/display.hpp"
