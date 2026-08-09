#include "systemMonitor.hpp"
#include "display/display.hpp"
#include "task/task.hpp"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <utility>

// 额外缓冲区，防止任务数在两次采样间变化导致数组不够
static constexpr UBaseType_t ARRAY_SIZE_OFFSET = 5;

// ── 字节数格式化（KB / MB） ──
static void formatSize(char* out, size_t cap, uint32_t bytes)
{
	if (bytes >= 1024 * 1024)
		snprintf(out, cap, "%.1fMB", bytes / (1024.0f * 1024.0f));
	else
		snprintf(out, cap, "%" PRIu32 "KB", bytes / 1024);
}

void SystemMonitor::start(Display* display)
{
	if (m_running)
		return;
	m_running = true;
	m_display = display;

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

	Task::addTask(pollTask, "sysMonitor", this, pdMS_TO_TICKS(1000), Task::Affinity::None);
	ESP_LOGI(TAG, "系统监视器已启动%s", m_brief ? "（简略模式）" : "");
}

void SystemMonitor::stop()
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

bool SystemMonitor::ensureCapacity(UBaseType_t needed)
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

void SystemMonitor::logMemoryAndFps()
{
	// 内部 RAM（SRAM）与 PSRAM 的已用/总量（与现代系统一致，显示 used 而非 free）
	uint32_t ramFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
	uint32_t ramTotal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
	uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	uint32_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

	uint32_t ramUsed = ramTotal - ramFree;
	uint32_t psramUsed = psramTotal - psramFree;

	char ramUsedS[16], ramTotalS[16], psramUsedS[16], psramTotalS[16];
	formatSize(ramUsedS, sizeof(ramUsedS), ramUsed);
	formatSize(ramTotalS, sizeof(ramTotalS), ramTotal);
	formatSize(psramUsedS, sizeof(psramUsedS), psramUsed);
	formatSize(psramTotalS, sizeof(psramTotalS), psramTotal);

	uint32_t ramUsedPct = (ramTotal > 0) ? ramUsed * 100ULL / ramTotal : 0;
	uint32_t psramUsedPct = (psramTotal > 0) ? psramUsed * 100ULL / psramTotal : 0;

	// FPS：display 未初始化（lv_disp 为空）时跳过，避免 esp_lv_adapter 报错
	uint32_t fps = 0;
	if (m_display && m_display->getLvglDisplay())
		fps = m_display->getFps();

	ESP_LOGI(TAG, "RAM:%s/%s(%" PRIu32 "%%) PSRAM:%s/%s(%" PRIu32 "%%) FPS:%" PRIu32,
		ramUsedS, ramTotalS, ramUsedPct, psramUsedS, psramTotalS, psramUsedPct, fps);
}

TickType_t SystemMonitor::pollTask(void* param)
{
	auto* self = static_cast<SystemMonitor*>(param);
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
		self->logMemoryAndFps();

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
	self->logMemoryAndFps();

	// ── 交换缓冲区（prev ← curr），仅 swap 指针，零分配 ──
	std::swap(self->m_prevSnapshot, self->m_currSnapshot);
	self->m_prevCount = actualCount;
	self->m_prevRunTime = currentRunTime;

	return pdMS_TO_TICKS(1000);
}
