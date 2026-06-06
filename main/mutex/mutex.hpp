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

class Lock
{
public:
	Lock(const Mutex& mutex) : mutex{ mutex } { mutex.lock(); }
	~Lock() { mutex.unlock(); }

private:
	const Mutex& mutex;
};
