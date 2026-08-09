#pragma once

#include "app/app.hpp"
#include "aiAgent/aiProtocol.hpp"
#include "market/marketClient.hpp"
#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief 游戏市场 App
 *
 * 从市场服务器拉取列表（Lua 脚本 + ELF 原生插件混合）→ 浏览 → 详情（截图/简介）
 * → 下载/更新/卸载。Lua 存 /root/lua/market/（运行走脚本管理），ELF 装 /root/elf/（返回桌面建卡）。
 *
 * 布局：
 * ┌────────────────────────────────────┐
 * │ ← 返回   游戏市场          [状态]   │  标题行
 * ├────────────────────────────────────┤
 * │ ▶ 经典贪吃蛇    ★4.8  v1.0 12KB    │  列表行（选中高亮，左上角 Lua/ELF 类型徽标）
 * │   双人乒乓      ★4.5  v1.0  9KB    │
 * │   ...                              │
 * ├────────────────────────────────────┤
 * │ [↻ 刷新] [ℹ 详情] [⇩ 下载/更新]    │  底部操作栏
 * └────────────────────────────────────┘
 */
class MarketApp final : public App
{
public:
	constexpr static char TAG[] = "MarketApp";

	MarketApp(Display* display);
	~MarketApp() override;

	/** @brief AI open_app 静态工厂（能力表用） */
	static App* newApp(Display* display, const cJSON* params) {
		(void)params;
		return new MarketApp(display);
	}

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

	// ── AI 能力 ──
	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;

private:
	// ── 数据 ──
	std::vector<MarketScript> m_list;
	int8_t m_selectedIdx{ -1 };
	bool m_loading{ false };
	bool m_hasFetched{ false };
	std::string m_serverUrl;

	// ── 模式 / 焦点组 ──
	enum Mode : int8_t {
		MODE_BROWSE = 0,   // 浏览列表
		MODE_DETAIL = 1,   // 详情视图
	};
	Mode m_mode{ MODE_BROWSE };

	enum FocusGroup : int8_t {
		FOCUS_LIST = 0,    // 脚本列表
		FOCUS_BOTTOM = 1,  // 底部操作按钮
		FOCUS_DETAIL = 2,  // 详情按钮（0=返回, 1=下载/更新, 2=卸载，已装时才有 2）
	};
	FocusGroup m_focusGroup{ FOCUS_LIST };
	int8_t m_focusBottomIdx{ 0 }; // 0=刷新, 1=详情, 2=下载运行
	static constexpr int BOTTOM_BTN_COUNT = 3;
	static constexpr int GRID_COLS = 3;   // 网格列数（配合 lv_pct(32) 卡片宽）
	int8_t m_focusDetailIdx{ 0 }; // 0=返回, 1=下载/更新, 2=卸载

	// ── UI 对象 ──
	lv_obj_t* m_container{};
	lv_obj_t* m_titleRow{};
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_listContainer{};
	lv_obj_t* m_statusLabel{};        // 状态（加载中/错误/服务器地址）
	std::vector<lv_obj_t*> m_entryCards;      // 网格卡片
	std::vector<lv_obj_t*> m_cardImages;      // 卡片截图 image
	std::vector<lv_obj_t*> m_cardThPh;        // 卡片截图占位（评分星）
	std::vector<lv_obj_t*> m_cardBadges;      // 卡片"已装"角标（默认隐藏，下载/卸载后动态刷新）
	std::vector<bool> m_thumbRequested;       // 缩略图是否已请求
	std::vector<bool> m_thumbLoaded;          // 缩略图是否已加载
	std::vector<int> m_thumbQueue;            // 待加载缩略图队列（串行）
	bool m_thumbLoading{ false };
	std::vector<bool> m_downloaded;           // 脚本是否已下载到本地（Lua: /root/lua/market/；ELF: /root/elf/<id>/）

	lv_obj_t* m_bottomBar{};
	lv_obj_t* m_bottomBtns[BOTTOM_BTN_COUNT]{};

	// 详情视图
	lv_obj_t* m_detailBox{};          // 全屏遮罩
	lv_obj_t* m_detailCard{};         // 详情卡片
	lv_obj_t* m_detailImage{};        // 截图
	lv_obj_t* m_detailImagePh{};      // 截图占位文字
	lv_obj_t* m_detailMeta{};         // 类型/作者/版本/评分/下载/更新时间
	lv_obj_t* m_detailDesc{};         // 简介
	lv_obj_t* m_detailBtnBack{};
	lv_obj_t* m_detailBtnRun{};
	lv_obj_t* m_detailBtnUninstall{};   // 详情卸载按钮（Lua/ELF 已装时均显示）
	bool m_detailUninstallVisible{ false };
	std::string m_detailImagePath;    // 截图临时文件（memFS）
	bool m_detailImageLoaded{ false };

	// ── 下载 ──
	bool m_downloading{ false };

	// ── 输入去重 ──
	TickType_t m_nextActionTime{};
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	uint16_t m_prevButtons[MaxHosts][MaxPlayers]{};
	static constexpr TickType_t ACTION_COOLDOWN = 500;
	static constexpr TickType_t MOVE_DELAY_FIRST = 300;
	static constexpr TickType_t MOVE_DELAY = 120;

	// ── 内部方法 ──
	void fetchList();
	void rebuildList();
	void selectNext();
	void selectPrev();
	void selectUp();
	void selectDown();
	void queueThumbnail(int idx);
	void processThumbQueue();
	void ensureVisibleThumbs();
	bool isDownloaded(int idx) const;
	static std::string localPathFor(const std::string& author, const std::string& id, const std::string& download);
	static std::string localSubPathFor(const std::string& author, const std::string& id, const std::string& download);
	void refreshDownloadedState();
	void updateActionLabels();
	void applyFocus();

	void showDetail();
	void hideDetail();
	void loadScreenshot(const MarketScript& s);
	void download();

	// ── ELF 插件 ──
	void downloadElf();                                  // 下载/更新 ELF 插件包（manifest + .so + icon）
	void downloadElfDone(int selIdx, bool wasDownloaded, const std::string& id, const std::string& author);
	void downloadFail(const std::string& msg);
	void uninstall();                                    // 卸载（按类型分流：ELF unload+删目录 / Lua 删目录）
	void uninstallElf();                                 // ELF：unload + 递归删目录
	void uninstallLua();                                 // Lua：递归删 /root/lua/market/<作者>/<游戏>/ + 清空作者目录
	void updateBadge(int idx);                           // 按 m_downloaded 显示/隐藏第 idx 张卡片"已装"角标
	static std::string elfDirPath(const MarketScript& s);    // /root/elf/<id>
	static std::string elfMainName(const MarketScript& s);   // <id>.so（s.main 优先）

	void cleanupLegacyFlatFiles();
	void setStatus(const char* text, lv_color_t color);
	static std::string sanitizeSegment(const std::string& seg);
};
