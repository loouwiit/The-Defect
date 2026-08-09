#pragma once

#include <cstdint>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class Display;

/**
 * @brief 系统监视器（单例）：CPU 利用率 + 内存（内部 RAM / PSRAM）+ 显示 FPS
 *
 * 整合原 CpuMonitor（CPU 差分采样）与 fpsMonitor（独立 Task），并新增内存统计：
 *   - CPU：通过 FreeRTOS uxTaskGetSystemState() 差分采样（简略模式走 IDLE 计数器轻量路径）
 *   - 内存：内部 RAM 与 PSRAM 的已用/总量（heap_caps_get_free/total_size 差值，显示 used 与现代系统一致）
 *   - FPS：Display::getFps()（display 未初始化时跳过）
 *
 * 使用方式:
 *   SystemMonitor::instance().setBrief(true);
 *   SystemMonitor::instance().start(&display);   // main.cpp 中调用
 *   SystemMonitor::instance().stop();            // 停止采样（可选）
 */
class SystemMonitor
{
public:
	static constexpr char TAG[] = "SysMon";

	static SystemMonitor& instance()
	{
		static SystemMonitor s_instance;
		return s_instance;
	}

	/** @brief 启动周期性采样（首次采样 + 注册 pollTask）；display 用于读取 FPS（可安全传未初始化的 display） */
	void start(Display* display);

	/** @brief 停止采样并释放所有缓存 */
	void stop();

	/** @brief 设置简略模式（只输出 CPU 一行 + 内存/FPS 一行，不输出各任务详情） */
	void setBrief(bool brief) { m_brief = brief; }

private:
	SystemMonitor() = default;
	~SystemMonitor() = default;
	SystemMonitor(const SystemMonitor&) = delete;
	SystemMonitor& operator=(const SystemMonitor&) = delete;

	static TickType_t pollTask(void* param);

	/** @brief 确保缓冲区容量足够，不足则重分配 */
	bool ensureCapacity(UBaseType_t needed);

	/** @brief 输出内存（内部 RAM / PSRAM）+ FPS 摘要行（两种模式共用） */
	void logMemoryAndFps();

	Display* m_display{ nullptr };
	bool m_running{ false };
	bool m_brief{ false };

	// ── CPU 双缓冲快照（持久分配，避免每轮 malloc/free） ──
	TaskStatus_t* m_prevSnapshot{ nullptr };  // 上一轮快照
	TaskStatus_t* m_currSnapshot{ nullptr };  // 本轮快照（可复用缓冲区）
	UBaseType_t m_capacity{ 0 };              // 每个缓冲区的 capacity（元素个数）

	// ── 匹配标记缓冲区（持久分配，2 × m_capacity 元素） ──
	//   [0..cap-1]        → prevMatched
	//   [cap..2*cap-1]    → currMatched
	bool* m_matchedBuf{ nullptr };

	// ── 上一轮采样值 ──
	UBaseType_t m_prevCount{ 0 };
	configRUN_TIME_COUNTER_TYPE m_prevRunTime{ 0 };
	configRUN_TIME_COUNTER_TYPE m_prevIdleRunTime[2]{ 0, 0 };
};
