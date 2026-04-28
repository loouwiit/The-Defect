#include "task.hpp"
#include <esp_log.h>

Task Task::head{};

EXT_RAM_BSS_ATTR static Thread* deamonThread{};
EXT_RAM_BSS_ATTR static size_t deamonThreadCount{};
EXT_RAM_BSS_ATTR static bool deamonRunning = false;

void Task::init(size_t deamonThreadCount)
{
	if (deamonRunning) return;
	if (deamonThreadCount == 0) return;
	deamonRunning = true;
	::deamonThreadCount = deamonThreadCount;
	deamonThread = new Thread[deamonThreadCount]{};
	for (size_t i = 0; i < deamonThreadCount; i++)
		deamonThread[i] = { daemonMain, "taskDaemon", (void*)i, Task::Priority::Deamon, 4096 };

	auto atLeastOneStarted = false;
	for (size_t i = 0; i < deamonThreadCount; i++)
		if (deamonThread[i].isRunning())
			atLeastOneStarted = true;

	deamonRunning = atLeastOneStarted;
	if (deamonRunning) ESP_LOGI(TAG, "deamon started");
	else ESP_LOGE(TAG, "demaon start failed");
}

// void Task::deinit()
// {
// 	deamonRunning = false;
// 	for (Task* nowTask = head.next->next; nowTask->last != &head; nowTask = nowTask->next)
// 		removeTask(nowTask->last);
// 	// bug here, who release the stack?
// }

const char* const* Task::dumpTask()
{
	auto count = 0;
	for (Task* nowTask = head.next; nowTask != &head; nowTask = nowTask->next)
		count++;

	char** ret = new char* [count + 2]; // for nowTime and \0

	ret[0] = new char[32];
	sprintf(ret[0], "now time = %lu\n", xTaskGetTickCount());

	auto i = 1;
	for (Task* nowTask = head.next; nowTask != &head; nowTask = nowTask->next)
	{
		ret[i] = new char[64];
		sprintf(ret[i], "[%lu]: task %s(%p)\n", nowTask->nextCallTick, nowTask->name, nowTask->param);
		i++;
	}
	ret[i] = new char[1] {'\0'};

	return ret;
}

void Task::deleteDump(const char* const* dump)
{
	using String = const char*;
	const String* i = dump;
	while ((*i)[0] != '\0')
	{
		delete[] * i;
		i++;
	}
	delete[] * i;
	delete[] dump;
}

void Task::daemonMain(void* param)
{
	size_t deamonThreadId = (size_t)param;

	while (deamonRunning)
	{
		TickType_t closestTime = infinityTime;
		TickType_t nowTime = xTaskGetTickCount();
		for (Task* nowTask = head.next; nowTask != &head; nowTask = nowTask->next)
		{
			if (nowTask->nextCallTick < nowTime &&
				(nowTask->affinityId == deamonThreadId || nowTask->affinityId >= deamonThreadCount)
				&& nowTask->mutex.try_lock())
			{
				if (nowTask->affinityId == Affinity::NotAssigned) [[unlikely]]
					nowTask->affinityId = deamonThreadId;

				TickType_t intervalTick = nowTask->function(nowTask->param);
				if (intervalTick == infinityTime)
				{
					nowTask->nextCallTick = infinityTime;
					nowTask->mutex.unlock();
					removeTask(nowTask);
					continue;
				}
				nowTask->nextCallTick = nowTime + intervalTick;
				nowTask->mutex.unlock();
			}
			if (nowTask->nextCallTick < closestTime)
				closestTime = nowTask->nextCallTick;
		}

		nowTime = xTaskGetTickCount(); // 函数调用可能导致时间流逝，在此处重新获取
		if (closestTime < nowTime)
			closestTime = nowTime + 1; // 至少睡1tick
		auto sleepTime = closestTime - nowTime;
		if (sleepTime > maxSleepTime)
			sleepTime = maxSleepTime; // 最多睡maxSleepTime
		vTaskDelay(sleepTime);
	}
}

Task* Task::addTask(Function_t function, const char* name, void* param, TickType_t firstCallTick, Affinity affinity)
{
	if (firstCallTick == infinityTime) return nullptr;

	Task* task = new Task{};

	Lock lock{ task->mutex };

	task->function = function;
	task->param = param;
	task->name = name;
	setAffinity(task, affinity);
	task->nextCallTick = xTaskGetTickCount() + firstCallTick;

	task->last = head.last;
	task->next = &head;

	task->next->last = task;
	task->last->next = task;

	return task;
}

void Task::removeTask(Task* task)
{
	task->mutex.lock();
	task->nextCallTick = infinityTime;
	task->last->next = task->next;
	task->next->last = task->last;
	task->mutex.unlock();

	delete task;
}

void Task::setAffinity(Task* task, Affinity affinity)
{
	if (affinity < deamonThreadCount ||
		affinity == Affinity::None ||
		affinity == Affinity::NotAssigned)
		task->affinityId = affinity;
	else task->affinityId = Affinity::NotAssigned;
}

Thread::Thread(Function_t function, const char* name, void* param, Priority priority, size_t stackSize)
{
	data.stack = new StackType_t[stackSize];
	data.task = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
	data.handle = xTaskCreateStatic(function, name, stackSize, param, priority, data.stack, data.task);
	if (data.handle == nullptr)
	{
		ESP_LOGE(TAG, "handle = nullptr! task creat failed");
		free(data.task);
		data.task = nullptr;
		delete[] data.stack;
		data.stack = nullptr;
	}
}

Thread::~Thread()
{
	if (data.handle == nullptr) return;

	ThreadData* saveCopy = new ThreadData;
	*saveCopy = data;

	Task::addTask([](void* param) ->TickType_t
		{
			auto& self = *(ThreadData*)param;
			self.handle = nullptr;
			free(self.task);
			self.task = nullptr;
			delete[] self.stack;
			self.stack = nullptr;
			delete& self;
			return Task::infinityTime;
		}, "delete thread", saveCopy, 100);
	vTaskDelete(data.handle);
}

bool Thread::isRunning()
{
	return data.handle != nullptr;
}

void Thread::suspend()
{
	vTaskSuspend(data.handle);
}

void Thread::resume()
{
	vTaskResume(data.handle);
}
