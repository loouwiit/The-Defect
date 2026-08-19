#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Mutex {
public:
	Mutex()
	{
		rtosMutex = xSemaphoreCreateMutex();
	}

	// ⚠ 互斥锁不可拷贝/移动：句柄按值拷贝 + 临时对象析构 vSemaphoreDelete
	//   会留下悬空句柄（UAF）→ 后续 lock() 在已释放队列上 assert 崩溃。
	Mutex(const Mutex&) = delete;
	Mutex& operator=(const Mutex&) = delete;
	Mutex(Mutex&&) = delete;
	Mutex& operator=(Mutex&&) = delete;

	~Mutex()
	{
		// ⚠ 析构前确保无并发持有者：非递归锁阻塞 take 直到独占（当前调用方须未持有
		//   该锁，否则同任务重复 take 永久阻塞——本仓库 Mutex 均为常驻单例，不会触发）。
		//   原实现 `while (take != pdTRUE) vTaskDelay(1)` 中 take 用 portMAX_DELAY 永不返回
		//   非 pdTRUE → 循环体为死代码，已简化。
		xSemaphoreTake(rtosMutex, portMAX_DELAY);
		vSemaphoreDelete(rtosMutex);
	}

	bool try_lock() const
	{
		return xSemaphoreTake(rtosMutex, 0) == pdTRUE;
	}

	void lock() const
	{
		xSemaphoreTake(rtosMutex, portMAX_DELAY);
	}

	void unlock() const
	{
		xSemaphoreGive(rtosMutex);
	}

private:
	SemaphoreHandle_t rtosMutex;
};

// 递归互斥锁：同一 task 可嵌套获取（xSemaphoreCreateRecursiveMutex）。
// 用于 AppStackManager 等"持锁期间可能递归调用自身操作"的场景
// （如 push 内 App::init() 再 pushApp）。
class RecursiveMutex {
public:
	RecursiveMutex()
	{
		rtosMutex = xSemaphoreCreateRecursiveMutex();
	}

	// ⚠ 同上：不可拷贝/移动（悬空句柄 UAF）
	RecursiveMutex(const RecursiveMutex&) = delete;
	RecursiveMutex& operator=(const RecursiveMutex&) = delete;
	RecursiveMutex(RecursiveMutex&&) = delete;
	RecursiveMutex& operator=(RecursiveMutex&&) = delete;

	~RecursiveMutex()
	{
		// ⚠ 阻塞 take 直到独占（析构前提：当前调用方未持有；递归锁可安全重复 take
		//   计数归零后删除）。原实现 while 循环体为死代码，已简化。
		xSemaphoreTakeRecursive(rtosMutex, portMAX_DELAY);
		vSemaphoreDelete(rtosMutex);
	}

	bool try_lock() const
	{
		return xSemaphoreTakeRecursive(rtosMutex, 0) == pdTRUE;
	}

	void lock() const
	{
		xSemaphoreTakeRecursive(rtosMutex, portMAX_DELAY);
	}

	void unlock() const
	{
		xSemaphoreGiveRecursive(rtosMutex);
	}

private:
	SemaphoreHandle_t rtosMutex;
};

class Lock
{
public:
	Lock(const Mutex& mutex) : m_mutex{ &mutex } { m_mutex->lock(); }
	Lock(const RecursiveMutex& mutex) : m_rmutex{ &mutex } { m_rmutex->lock(); }
	~Lock() { if (m_rmutex) m_rmutex->unlock(); else m_mutex->unlock(); }

private:
	const Mutex* m_mutex{};
	const RecursiveMutex* m_rmutex{};
};
