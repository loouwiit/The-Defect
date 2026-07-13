#pragma once

#include "app/app.hpp"
#include "lvgl.h"
#include "aiAgent/aiProtocol.hpp"
#include "aiAgent/aiConfig.hpp"
#include <string>
#include <vector>
#include <functional>

/**
 * @brief AI Agent App
 *
 * 提供触屏键盘输入 → 调用 OpenAI 兼容 API → AI 返回结构化 JSON → 设备执行操作。
 *
 * UI 布局：
 * ┌──────────────────────────┐
 * │  ← 返回         AI 助手  │  顶部栏
 * ├──────────────────────────┤
 * │  🧑: 输入文本            │  对话记录（滚动）
 * │  🤖: 回复/结果           │
 * │  ─── ⏳ 思考中... ───   │  loading
 * ├──────────────────────────┤
 * │ [输入框.............] [▶]│  输入行
 * ├──────────────────────────┤
 * │  Q W E R T Y U I O P    │  LVGL 键盘
 * │  ...                    │
 * └──────────────────────────┘
 */
class AiAgentApp final : public App
{
public:
	constexpr static char TAG[] = "AiAgentApp";

	AiAgentApp(Display* display);
	~AiAgentApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	// ── UI 组件 ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_chatLog{};          // 对话记录（lv_textarea 只读 / lv_label 列表）
	lv_obj_t* m_inputField{};       // 输入框 (lv_textarea)
	lv_obj_t* m_keyboard{};         // LVGL 触屏键盘
	lv_obj_t* m_sendBtn{};          // 发送按钮
	lv_obj_t* m_loadingLabel{};     // "思考中..." 标签
	lv_obj_t* m_contextHint{};      // 上下文提示标签（显示当前前台 App 信息）

	// ── 状态 ──
	bool m_isWaiting = false;
	int m_followUpCount = 0;
	static constexpr int MAX_FOLLOW_UP = 3;
	std::string m_lastUserInput;    // 上一轮用户输入
	std::string m_lastSystemPrompt; // 上一轮 system prompt（含 app_context）
	std::vector<std::string> m_chatHistory;
	AiConfig m_config;
	TickType_t m_nextActionTime{};
	static constexpr TickType_t ACTION_DELAY = 300;

	// ── UI 构建 ──
	void buildUi();
	void addChatEntry(const std::string& role, const std::string& text);
	void setLoading(bool loading);
	void updateContextHint();

	// ── 发送逻辑 ──
	void onSend();
	void onAiResponse(bool success, const std::string& responseText, const std::string& errorMsg);
	/** 收集当前轮 action 产生的观测数据（如 system_info），非空则发起下一轮 */
	void doFollowUp(const std::vector<AiAction>& actions, const std::string& userInput);

	// ── 回调 ──
	static void onBackBtnCb(lv_event_t* e);
	static void onSendBtnCb(lv_event_t* e);
	static void onKeyboardReadyCb(lv_event_t* e);
};
