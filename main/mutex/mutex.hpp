#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Mutex {
public:
	Mutex()
	{
		rtosMutex = xSemaphoreCreateMutex();
	}

	// 互斥锁不可拷贝/移动：句柄按值拷贝 + 临时对象析构 → 悬空句柄
	Mutex(const Mutex&) = delete;
	Mutex& operator=(const Mutex&) = delete;
	Mutex(Mutex&&) = delete;
	Mutex& operator=(Mutex&&) = delete;

	~Mutex()
	{
		// 析构前确保无并发持有者（非递归锁 take 会阻塞直到独占）
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

	// 不可拷贝/移动（悬空句柄 UAF）
	RecursiveMutex(const RecursiveMutex&) = delete;
	RecursiveMutex& operator=(const RecursiveMutex&) = delete;
	RecursiveMutex(RecursiveMutex&&) = delete;
	RecursiveMutex& operator=(RecursiveMutex&&) = delete;

	~RecursiveMutex()
	{
		// 阻塞 take 直到独占，递归锁可安全重复 take 计数归零后删除
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
