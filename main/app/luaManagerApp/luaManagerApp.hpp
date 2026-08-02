#pragma once

#include "app/app.hpp"
#include "aiAgent/aiProtocol.hpp"
#include <string>
#include <vector>
#include <ctime>
#include <cstdint>

/**
 * @brief Lua 脚本管理器 App
 *
 * 提供 Lua 脚本的浏览、运行、删除、重命名功能，
 * 以及 AI 生成新游戏的入口。
 *
 * 布局：
 * ┌─────────────────────────────────┐
 * │ ← 返回    Lua 脚本管理          │
 * ├─────────────────────────────────┤
 * │ ▶ snake                        │  列表行（选中高亮）
 * │   📄 1.2 KB  2026-07-27        │  子信息行
 * ├─────────────────────────────────┤
 * │   ...                           │
 * ├─────────────────────────────────┤
 * │ [▶ 运行] [🗑 删除] [✏ 重命名]  │  底部操作栏
 * │ [🤖 AI 生成新游戏]              │
 * └─────────────────────────────────┘
 */
class LuaManagerApp final : public App
{
public:
	constexpr static char TAG[] = "LuaManagerApp";

	LuaManagerApp(Display* display);
	~LuaManagerApp() override;

	/** @brief AI open_app 静态工厂（能力表用） */
	static App* newApp(Display* display, const cJSON* params) {
		(void)params;
		return new LuaManagerApp(display);
	}

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

	// ── AI 能力 ──
	std::string getAiContext() override;
	// bool executeAiAction(const AiAction& action) override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;

private:
	// ── 脚本数据结构 ──
	struct ScriptEntry {
		std::string name;        // 完整文件名（含 .lua）
		std::string displayName; // 去掉 .lua 后缀的显示名
		std::string path;        // 完整 FAT 路径
		std::string shortPath;   // 简短路径前缀（"game/" 或 "lua/"）
		size_t sizeBytes{};
	};
	std::vector<ScriptEntry> m_scripts;
	int8_t m_selectedIdx{ 0 };

	// ── 模式 ──
	enum Mode : int8_t {
		MODE_BROWSE = 0,    // 浏览列表
		MODE_RENAME_INPUT,  // 重命名输入中
		MODE_CONFIRM_DEL,   // 确认删除
	};
	Mode m_mode{ MODE_BROWSE };

	// ── 焦点组 ──
	enum FocusGroup : int8_t {
		FOCUS_LIST = 0,    // 脚本列表
		FOCUS_BOTTOM = 1,  // 底部操作按钮
		FOCUS_CONFIRM = 2, // 删除确认对话框按钮
	};
	FocusGroup m_focusGroup{ FOCUS_LIST };
	int8_t m_focusBottomIdx{ 0 }; // 0=运行, 1=删除, 2=重命名, 3=AI生成
	static constexpr int BOTTOM_BTN_COUNT = 4;

	// ── UI 对象 ──
	lv_obj_t* m_container{};
	lv_obj_t* m_titleRow{};
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_listContainer{};              // 列表容器（内有行）
	lv_obj_t* m_emptyLabel{};                 // 空列表提示
	std::vector<lv_obj_t*> m_entryRows;       // 列表行对象
	std::vector<lv_obj_t*> m_entryInfoLabels; // 信息标签

	lv_obj_t* m_bottomBar{};
	lv_obj_t* m_bottomBtns[BOTTOM_BTN_COUNT]{};

	// ── 行宽常量 ──
	static constexpr int ROW_H = 52;
	static constexpr int MAX_VISIBLE = 11;

	// ── 删除确认 ──
	lv_obj_t* m_confirmBox{};
	lv_obj_t* m_confirmLabel{};
	lv_obj_t* m_confirmCancelBtn{};
	lv_obj_t* m_confirmOkBtn{};
	int8_t m_focusConfirmIdx{0};  // 0=取消, 1=确认删除
	std::string m_pendingDeletePath;

	// ── 重命名 ──
	lv_obj_t* m_renameContainer{};
	lv_obj_t* m_renameField{};
	lv_obj_t* m_renameKeyboard{};
	bool m_renameKeyboardVisible{};

	// ── 定时器 ──
	lv_timer_t* m_refreshTimer{};

	// ── 输入去重 ──
	TickType_t m_nextActionTime{};
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	uint16_t m_prevButtons[MaxHosts][MaxPlayers]{};
	static constexpr TickType_t ACTION_COOLDOWN = 500;
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 内部方法 ──
	void scanScripts();
	void rebuildList();
	void runSelected();
	void deleteSelected();
	void confirmDelete();
	void startRename();
	void submitRename();
	void cancelRename();
	void startAiGeneration();
	void applyFocus();

	// 列表选择切换
	void selectNext();
	void selectPrev();
	void selectFirst();
	void selectLast();
};
