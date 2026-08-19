#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "display/display.hpp"
#include "gamepad/gamepadState.hpp"
#include "audio/Audio.hpp"
#include "mutex/mutex.hpp"
#include "gui.hpp"

/**
 * @brief 通用系统弹窗服务（lv_layer_sys）
 *
 * 系统级单例，提供 FIFO 队列管理的 `lv_layer_sys()` 弹窗。
 * 任意模块可通过唯一的 Key 显示弹窗，同 key 幂等更新内容；
 * 多弹窗时按 FIFO 顺序排队，前一个关闭后自动显示下一个。
 *
 * 关闭方式：
 * - 触摸 [知道了] 按钮
 * - 任意手柄按键（边沿检测）
 *
 * 已有用户：
 * - BatteryMonitor：低电量提醒（key="battery"，accent=DANGER，循环提示音）
 * - OtaManager：OTA 升级结果提醒（key="ota"，accent 按结果变化）
 *
 * 使用方式（main.cpp，AI Overlay 之后）：
 * @code
 *   SystemPopup::instance().start(&display);
 * @endcode
 */

/** @brief 弹窗选项（传给 SystemPopup::show()，定义在类外以支持默认参数） */
struct SystemPopupOptions
{
	lv_color_t               accent{ GUI::Color::PRIMARY };  // 卡片边框色
	const char*              buttonText{ "知道了" };          // 按钮文字
	const char*              footerText{ "" };                // 按钮上方提示行（空=不显示）
	bool                     playAlertSound{ false };         // 是否循环播放提示音（关闭时停止）
	std::function<void()>    onDismiss{ nullptr };            // 用户关闭回调（按钮/手柄；auto hide 不触发）
};

class SystemPopup
{
public:
	static SystemPopup& instance();

	/** @brief 唯一标识弹窗类型（用于幂等更新与定点隐藏） */
	using Key = const char*;
	using Options = SystemPopupOptions;
	static constexpr Key kBattery = "battery";
	static constexpr Key kOta      = "ota";

	/** @brief 初始化：建弹窗 UI（初始 hidden）+ 手柄关闭定时器 */
	bool start(Display* display);

	/** @brief 停止并释放 UI */
	void stop();

	/** @brief 显示弹窗。同 key 已显示→原地更新内容；同 key 已排队→更新排队项；否则入队 */
	void show(Key key, const char* title, const char* body, const Options& opts = Options{});

	/** @brief 隐藏弹窗。显示中→隐藏+推进队列；排队中→移除。userDismiss=false 不触发 onDismiss */
	void hide(Key key, bool userDismiss = true);

	/** @brief 是否有弹窗正在显示（VirtualGamepad 60fps 门控用，std::atomic 零锁读取） */
	bool isShown() const { return m_shown.load(std::memory_order_relaxed); }

private:
	SystemPopup() = default;
	~SystemPopup() = default;
	SystemPopup(const SystemPopup&) = delete;
	SystemPopup& operator=(const SystemPopup&) = delete;

	// ── 队列条目 ──
	static constexpr size_t kMaxQueue = 4;
	struct PopupReq {
		Key           key{};
		std::string   title;
		std::string   body;
		Options       opts{};
	};

	// ── UI 构建与控制（须持 LVGL 锁） ──
	bool buildUi();
	void showCurrent();              // 用队列首项更新 UI 并显示
	void hideCurrent(bool userDismiss);
	void advanceQueue();             // hideCurrent 后调用：弹出首项 + 显示下一项

	// ── 手柄按键关闭弹窗（LVGL 定时器，边沿检测） ──
	static void dismissTimerCb(lv_timer_t* t);

	// ── 提示音 ──
	void stopAlertSound();

	// ── 依赖 ──
	Display*          m_display{};
	bool              m_started{};
	lv_timer_t*       m_dismissTimer{};

	// ── 弹窗状态 ──
	std::atomic<bool> m_shown{ false };

	// ── 队列 + 当前显示项（Mutex 保护：show/hide 可任意线程调用） ──
	Mutex             m_mutex;
	PopupReq          m_current{};         // 正在显示的弹窗数据
	bool              m_currentValid{};    // m_current 是否有效（有内容）
	std::deque<PopupReq> m_queue;          // 排队中的弹窗（FIFO）
	std::string       m_bodyCache;         // 上次 body（去重 lv_label_set_text）
	AudioHandle       m_alertAudio{};      // 循环提示音（关闭时停止）

	// ── LVGL 对象（建在 lv_layer_sys() 上） ──
	lv_obj_t* m_scrim{};           // 全屏半透明遮罩
	lv_obj_t* m_card{};            // 居中弹窗卡片
	lv_obj_t* m_titleLabel{};
	lv_obj_t* m_bodyLabel{};
	lv_obj_t* m_footerLabel{};     // 可选提示行
	lv_obj_t* m_okBtn{};

	// ── 手柄关闭 ──
	uint16_t m_prevButtons[MaxPlayers]{};
};
