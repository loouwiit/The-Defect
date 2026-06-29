#include "cpuMonitor.hpp"
#include "task/task.hpp"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

// 额外缓冲区，防止任务数在两次采样间变化导致数组不够
static constexpr UBaseType_t ARRAY_SIZE_OFFSET = 5;

void CpuMonitor::start()
{
	if (m_running)
		return;
	m_running = true;

	if (m_brief)
	{
		// 简略模式：轻量初始化，不用 uxTaskGetSystemState
		m_prevRunTime = esp_timer_get_time();
		m_prevIdleRunTime[0] = ulTaskGetIdleRunTimeCounterForCore(0);
		m_prevIdleRunTime[1] = ulTaskGetIdleRunTimeCounterForCore(1);
	}
	else
	{
		// 详细模式：全量快照
		UBaseType_t taskCount = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
		if (!ensureCapacity(taskCount))
		{
			ESP_LOGE(TAG, "首次分配缓冲区失败");
			m_running = false;
			return;
		}

		m_prevCount = uxTaskGetSystemState(m_prevSnapshot, m_capacity, &m_prevRunTime);
		if (m_prevCount == 0)
		{
			ESP_LOGE(TAG, "首次采样失败（数组太小）");
			m_running = false;
			return;
		}
	}

	Task::addTask(pollTask, "cpuMonitor", this, pdMS_TO_TICKS(1000), Task::Affinity::None);
	ESP_LOGI(TAG, "CPU 监视器已启动%s", m_brief ? "（简略模式）" : "");
}

void CpuMonitor::stop()
{
	m_running = false;
	free(m_prevSnapshot);
	free(m_currSnapshot);
	free(m_matchedBuf);
	m_prevSnapshot = nullptr;
	m_currSnapshot = nullptr;
	m_matchedBuf = nullptr;
	m_capacity = 0;
	m_prevCount = 0;
}

bool CpuMonitor::ensureCapacity(UBaseType_t needed)
{
	if (needed <= m_capacity)
		return true;

	// 按需扩容（至少增长到 needed）
	auto newCap = needed + ARRAY_SIZE_OFFSET;
	auto* newPrev = (TaskStatus_t*)realloc(m_prevSnapshot, sizeof(TaskStatus_t) * newCap);
	auto* newCurr = (TaskStatus_t*)realloc(m_currSnapshot, sizeof(TaskStatus_t) * newCap);
	auto* newMatch = (bool*)realloc(m_matchedBuf, sizeof(bool) * newCap * 2);
	if (!newPrev || !newCurr || !newMatch)
	{
		free(newPrev);
		free(newCurr);
		free(newMatch);
		m_prevSnapshot = nullptr;
		m_currSnapshot = nullptr;
		m_matchedBuf = nullptr;
		m_capacity = 0;
		return false;
	}
	m_prevSnapshot = newPrev;
	m_currSnapshot = newCurr;
	m_matchedBuf = newMatch;
	m_capacity = newCap;
	return true;
}

TickType_t CpuMonitor::pollTask(void* param)
{
	auto* self = static_cast<CpuMonitor*>(param);
	if (!self->m_running)
		return Task::infinityTime;

	// ═══════════════════════════════════════════════════════
	// 简略模式：轻量路径 — 仅读 IDLE 计数器 + esp_timer
	// ═══════════════════════════════════════════════════════
	if (self->m_brief)
	{
		configRUN_TIME_COUNTER_TYPE currentRunTime = esp_timer_get_time();
		configRUN_TIME_COUNTER_TYPE totalElapsed = currentRunTime - self->m_prevRunTime;
		if (totalElapsed == 0)
			return pdMS_TO_TICKS(1000);

		configRUN_TIME_COUNTER_TYPE currIdle[2] = {
			ulTaskGetIdleRunTimeCounterForCore(0),
			ulTaskGetIdleRunTimeCounterForCore(1),
		};

		configRUN_TIME_COUNTER_TYPE idleElapsed[2] = {
			currIdle[0] - self->m_prevIdleRunTime[0],
			currIdle[1] - self->m_prevIdleRunTime[1],
		};

		self->m_prevRunTime = currentRunTime;
		self->m_prevIdleRunTime[0] = currIdle[0];
		self->m_prevIdleRunTime[1] = currIdle[1];

		uint32_t idlePct[2] = {
			(uint32_t)(idleElapsed[0] * 100ULL / totalElapsed),
			(uint32_t)(idleElapsed[1] * 100ULL / totalElapsed),
		};
		uint32_t coreBusy[2] = { 100 - idlePct[0], 100 - idlePct[1] };
		uint32_t totalBusy = (uint32_t)((2ULL * totalElapsed - idleElapsed[0] - idleElapsed[1]) * 100ULL / (2ULL * totalElapsed));

		ESP_LOGI(TAG, "CPU:%3" PRIu32 "%%, C0:%3" PRIu32 "%%, C1:%3" PRIu32 "%%",
			totalBusy, coreBusy[0], coreBusy[1]);

		return pdMS_TO_TICKS(1000);
	}

	// ═══════════════════════════════════════════════════════
	// 详细模式：全量快照 + 匹配 + 逐任务输出
	// ═══════════════════════════════════════════════════════
	UBaseType_t taskCount = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
	if (!self->ensureCapacity(taskCount))
	{
		ESP_LOGE(TAG, "缓冲区扩容失败");
		return pdMS_TO_TICKS(1000);
	}

	configRUN_TIME_COUNTER_TYPE currentRunTime;
	UBaseType_t actualCount = uxTaskGetSystemState(self->m_currSnapshot, self->m_capacity, &currentRunTime);
	if (actualCount == 0)
	{
		ESP_LOGE(TAG, "采样失败（数组太小）");
		return pdMS_TO_TICKS(1000);
	}

	configRUN_TIME_COUNTER_TYPE totalElapsed = currentRunTime - self->m_prevRunTime;
	if (totalElapsed == 0)
		return pdMS_TO_TICKS(1000);

	bool* prevMatched = self->m_matchedBuf;
	bool* currMatched = self->m_matchedBuf + self->m_capacity;
	memset(self->m_matchedBuf, 0, sizeof(bool) * self->m_capacity * 2);

	configRUN_TIME_COUNTER_TYPE idleElapsed[2] = { 0, 0 };

	ESP_LOGI(TAG, "──── CPU 占用率 ────");
	ESP_LOGI(TAG, "\t%-22s %s", "任务名", "占用率");

	for (UBaseType_t i = 0; i < actualCount; i++)
	{
		for (UBaseType_t j = 0; j < self->m_prevCount; j++)
		{
			if (prevMatched[j])
				continue;
			if (self->m_currSnapshot[i].xHandle == self->m_prevSnapshot[j].xHandle)
			{
				configRUN_TIME_COUNTER_TYPE taskElapsed =
					self->m_currSnapshot[i].ulRunTimeCounter - self->m_prevSnapshot[j].ulRunTimeCounter;

				uint32_t percent = (uint32_t)((taskElapsed * 100ULL) / totalElapsed);
				if (percent > 0)
					ESP_LOGI(TAG, "\t%-22s %3" PRIu32 "%%", self->m_currSnapshot[i].pcTaskName, percent);
				else
					ESP_LOGI(TAG, "\t%-22s  <1%%", self->m_currSnapshot[i].pcTaskName);

				// 累加 IDLE 时间用于计算总繁忙率
				if (strcmp(self->m_currSnapshot[i].pcTaskName, "IDLE0") == 0)
					idleElapsed[0] = taskElapsed;
				else if (strcmp(self->m_currSnapshot[i].pcTaskName, "IDLE1") == 0)
					idleElapsed[1] = taskElapsed;

				prevMatched[j] = true;
				currMatched[i] = true;
				break;
			}
		}

		// 让出调度，防止阻塞其他任务
		vTaskDelay(0);
	}

	for (UBaseType_t i = 0; i < actualCount; i++)
		if (!currMatched[i])
			ESP_LOGI(TAG, "\t%-22s  (新增)", self->m_currSnapshot[i].pcTaskName);
	for (UBaseType_t j = 0; j < self->m_prevCount; j++)
		if (!prevMatched[j])
			ESP_LOGI(TAG, "\t%-22s  (已删除)", self->m_prevSnapshot[j].pcTaskName);

	// ── 总繁忙率 & 各核心利用率 ──
	uint32_t idlePct[2] = {
		(uint32_t)(idleElapsed[0] * 100ULL / totalElapsed),
		(uint32_t)(idleElapsed[1] * 100ULL / totalElapsed),
	};
	uint32_t coreBusy[2] = { 100 - idlePct[0], 100 - idlePct[1] };
	uint32_t totalBusy = (uint32_t)((2ULL * totalElapsed - idleElapsed[0] - idleElapsed[1]) * 100ULL / (2ULL * totalElapsed));

	ESP_LOGI(TAG, "CPU:%3" PRIu32 "%%, C0:%3" PRIu32 "%%, C1:%3" PRIu32 "%%",
		totalBusy, coreBusy[0], coreBusy[1]);

	// ── 交换缓冲区（prev ← curr），仅 swap 指针，零分配 ──
	std::swap(self->m_prevSnapshot, self->m_currSnapshot);
	self->m_prevCount = actualCount;
	self->m_prevRunTime = currentRunTime;

	return pdMS_TO_TICKS(1000);
}
