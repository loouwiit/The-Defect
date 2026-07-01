#include "slave.hpp"
#include "task/task.hpp"
#include "esp_hosted.h"
#include <esp_log.h>

static constexpr char TAG[] = "Slave";

// ── Singleton ──

Slave& Slave::instance()
{
	static Slave inst{};
	return inst;
}

// ── 公共接口 ──

void Slave::start()
{
	if (m_started)
		return;
	m_started = true;

	m_evt = xEventGroupCreate();

	// 在后台 Task 中早期启动 C6 协处理器
	// 与主线程的显示/LVGL 初始化并行执行，缩短总启动时间
	// esp_hosted_init() 已由 __attribute__((constructor)) 在 app_main 前完成
	Task::addTask([](void* arg) -> TickType_t {
		auto& self = *static_cast<Slave*>(arg);
		ESP_LOGI(TAG, "早期启动 C6 协处理器...");
		if (esp_hosted_connect_to_slave() == ESP_OK)
			ESP_LOGI(TAG, "C6 协处理器已就绪");
		else
			ESP_LOGE(TAG, "C6 协处理器启动失败");
		xEventGroupSetBits(self.m_evt, kReadyBit);
		return Task::infinityTime;
	}, "c6Boot", this, 0, Task::Affinity::None);
}

void Slave::waitReady()
{
	if (!m_evt)
		return;
	// 等待 kReadyBit 置位。pdFALSE = 不清除该位，
	// 因此后续任意次 waitReady() 都立即返回
	xEventGroupWaitBits(m_evt, kReadyBit, pdFALSE, pdTRUE, portMAX_DELAY);
}

bool Slave::isReady() const
{
	return m_evt && (xEventGroupGetBits(m_evt) & kReadyBit);
}
