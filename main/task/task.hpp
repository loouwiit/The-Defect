#pragma once

#include <esp_log.h>
#include <esp_task.h>
#include "mutex/mutex.hpp"
#include <utility>

class Task
{
public:

	class Affinity
	{
	public:
		constexpr static size_t None = -1;
		constexpr static size_t NotAssigned = -2;

		Affinity() = default;
		Affinity(size_t affinity) : affinity{ affinity } {}
		Affinity(Affinity&) = default;
		Affinity(Affinity&&) = default;

		size_t affinity = None;
		operator size_t& ()
		{
			return affinity;
		}
	};

	class Priority
	{
	public:
		constexpr static UBaseType_t Deamon = 2;
		constexpr static UBaseType_t Verylow = 3;
		constexpr static UBaseType_t Low = 4;
		constexpr static UBaseType_t Normal = 5;
		constexpr static UBaseType_t High = 6;
		constexpr static UBaseType_t Veryhigh = 7;
		constexpr static UBaseType_t RealTime = 8;

		Priority() = default;
		Priority(UBaseType_t priority) : priority{ priority } {}
		Priority(Priority&) = default;
		Priority(Priority&&) = default;

		UBaseType_t priority = Normal;
		operator UBaseType_t& ()
		{
			return priority;
		}
	};

	constexpr static TickType_t infinityTime = portMAX_DELAY;
	constexpr static TickType_t maxSleepTime = 100;
	constexpr static TickType_t Immediately = 0;

	using Function_t = TickType_t(*)(void* param); // return value is how long the task will sleep

	static void init(size_t deamonThreadCount = 1);
	// static void deinit();

	static const char* const* dumpTask();
	static void deleteDump(const char* const* dump);

	static void daemonMain(void* param);

	static Task* addTask(Function_t function, const char* name, void* param = nullptr, TickType_t callTick = 0, Affinity affinity = Affinity::NotAssigned);
	static void removeTask(Task* task);

	static void setAffinity(Task* task, Affinity affinity); // 决定Task能不能切换线程执行

private:
	constexpr static char TAG[] = "Task";

	Task() = default;

	Function_t function = nullptr;
	void* param = nullptr;
	const char* name = nullptr;
	size_t affinityId = Affinity::None;
	TickType_t nextCallTick = 0;
	Mutex mutex{};

	Task* last = this;
	Task* next = this;

	static Task head;
};

class Thread
{
public:
	using Priority = Task::Priority;
	using Function_t = void (*)(void*);

	Thread() = default;
	Thread(Thread&& move) { operator=(std::move(move)); }
	Thread& operator=(Thread&& move) { std::swap(move.data, data); return *this; }

	Thread(Function_t function, const char* name, void* param = nullptr, Priority priority = Priority::Normal, size_t stackSize = 4096);

	~Thread();

	bool isRunning();

	void suspend();
	void resume();

private:
	constexpr static char TAG[] = "Thread";

	struct ThreadData
	{
		TaskHandle_t handle{};
		StackType_t* stack{};
		StaticTask_t* task{}; // must in internal ram
	};

	ThreadData data{};
};
