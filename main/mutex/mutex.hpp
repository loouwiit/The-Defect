#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Mutex {
public:
	Mutex()
	{
		rtosMutex = xSemaphoreCreateMutex();
	}

	~Mutex()
	{
		// 确保没有线程持有互斥量
		while (xSemaphoreTake(rtosMutex, portMAX_DELAY) != pdTRUE)
			vTaskDelay(1);
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

	~RecursiveMutex()
	{
		// 确保没有线程持有互斥量
		while (xSemaphoreTakeRecursive(rtosMutex, portMAX_DELAY) != pdTRUE)
			vTaskDelay(1);
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
