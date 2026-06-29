#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief CPU 利用率监视器（单例）
 *
 * 通过 FreeRTOS uxTaskGetSystemState() 差分采样，每秒输出各任务 CPU 占用率到串口。
 * 不区分 CPU 核心，百分比基于总 run time counter 增量计算。
 *
 * 使用方式:
 *   CpuMonitor::instance().start();   // 在 main.cpp 中调用
 *   CpuMonitor::instance().stop();    // 停止采样（可选）
 */
class CpuMonitor
{
public:
	static constexpr char TAG[] = "CpuMonitor";

	static CpuMonitor& instance()
	{
		static CpuMonitor s_instance;
		return s_instance;
	}

	/** @brief 启动周期性采样：首次采样 + 注册 pollTask */
	void start();

	/** @brief 停止采样并释放所有缓存 */
	void stop();

	/** @brief 设置简略模式（只输出总结行，不输出各任务详情） */
	void setBrief(bool brief) { m_brief = brief; }

private:
	CpuMonitor() = default;
	~CpuMonitor() = default;
	CpuMonitor(const CpuMonitor&) = delete;
	CpuMonitor& operator=(const CpuMonitor&) = delete;

	static TickType_t pollTask(void* param);

	/** @brief 确保缓冲区容量足够，不足则重分配 */
	bool ensureCapacity(UBaseType_t needed);

	bool m_running{ false };
	bool m_brief{ false };

	// ── 双缓冲区（持久分配，避免每轮 malloc/free） ──
	TaskStatus_t* m_prevSnapshot{ nullptr };  // 上一轮快照
	TaskStatus_t* m_currSnapshot{ nullptr };  // 本轮快照（可复用缓冲区）
	UBaseType_t m_capacity{ 0 };              // 每个缓冲区的 capacity（元素个数）

	// ── 匹配标记缓冲区（持久分配，2 × m_capacity 元素） ──
	//   [0..cap-1]        → prevMatched
	//   [cap..2*cap-1]    → currMatched
	bool* m_matchedBuf{ nullptr };

	UBaseType_t m_prevCount{ 0 };
	configRUN_TIME_COUNTER_TYPE m_prevRunTime{ 0 };

	// ── 简略模式专用：IDLE 累计计数器（避免调用 uxTaskGetSystemState） ──
	configRUN_TIME_COUNTER_TYPE m_prevIdleRunTime[2]{ 0, 0 };
};
