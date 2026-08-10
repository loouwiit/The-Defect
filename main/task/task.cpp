#include "task.hpp"
#include "mutex/mutex.hpp"
#include <esp_log.h>

Task Task::head{};

EXT_RAM_BSS_ATTR static Thread* deamonThread{};
EXT_RAM_BSS_ATTR static size_t deamonThreadCount{};
EXT_RAM_BSS_ATTR static bool deamonRunning = false;

// ⚠ 任务链表全局互斥锁：保护侵入式双向链表的遍历/插入/摘除。
//   daemon 遍历与 addTask（BLE task/UI task 任意线程）并发会导致
//   读已释放节点（UAF）/链表损坏。锁序约定：task->mutex → s_listMutex（无逆序，无死锁）。
static Mutex s_listMutex;

void Task::init(size_t deamonThreadCount)
{
	if (deamonRunning) return;
	if (deamonThreadCount == 0) return;
	deamonRunning = true;
	::deamonThreadCount = deamonThreadCount;
	deamonThread = new Thread[deamonThreadCount]{};
	for (size_t i = 0; i < deamonThreadCount; i++)
		deamonThread[i] = { daemonMain, "taskDaemon", (void*)i, Task::Priority::Deamon, 32768 };

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
	{
		Lock lock{ s_listMutex };
		for (Task* nowTask = head.next; nowTask != &head; nowTask = nowTask->next)
			count++;
	}

	char** ret = new char* [count + 2]; // for nowTime and \0

	ret[0] = new char[32];
	sprintf(ret[0], "now time = %lu", xTaskGetTickCount());

	auto i = 1;
	{
		Lock lock{ s_listMutex };
		for (Task* nowTask = head.next; nowTask != &head; nowTask = nowTask->next)
		{
			ret[i] = new char[64];
			sprintf(ret[i], "[%lu]: task %s(%p)", nowTask->nextCallTick, nowTask->name, nowTask->param);
			i++;
		}
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

		// 持锁遍历：找下一个到期且未被其他 daemon 占用的任务（try_lock 非阻塞）
		Task* toRun = nullptr;
		{
			Lock lock{ s_listMutex };
			for (Task* nowTask = head.next; nowTask != &head; nowTask = nowTask->next)
			{
				if (nowTask->nextCallTick < nowTime &&
					(nowTask->affinityId == deamonThreadId || nowTask->affinityId >= deamonThreadCount))
				{
					if (nowTask->mutex.try_lock())
					{
						if (nowTask->affinityId == Affinity::NotAssigned) [[unlikely]]
							nowTask->affinityId = deamonThreadId;
						toRun = nowTask;
						break;
					}
					// 已被其他 daemon 占用 → 跳过
				}
				if (nowTask->nextCallTick < closestTime)
					closestTime = nowTask->nextCallTick;
			}
		}

		if (toRun)
		{
			// 锁外执行任务函数（不持链表锁：函数内可安全 addTask/removeTask 其他任务）
			TickType_t intervalTick = toRun->function(toRun->param);

			if (intervalTick == infinityTime)
			{
				// 任务自灭：先解锁再移除（移除后不可再访问 toRun）
				toRun->mutex.unlock();
				removeTask(toRun);
				continue;
			}

			// 持锁更新下次调用时间。⚠ 此时仍持有 toRun->mutex，外部 removeTask 会
			// 阻塞等锁 → toRun 必然仍在链表中，安全更新。
			{
				Lock lock{ s_listMutex };
				toRun->nextCallTick = nowTime + intervalTick;
				if (toRun->nextCallTick < closestTime)
					closestTime = toRun->nextCallTick;
			}
			toRun->mutex.unlock();
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
	Lock listLock{ s_listMutex };   // 尾部插入持链表锁，防与 daemon 遍历并发

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
	{
		Lock listLock{ s_listMutex };   // 摘除持链表锁，防与 daemon 遍历并发
		task->last->next = task->next;
		task->next->last = task->last;
	}
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
