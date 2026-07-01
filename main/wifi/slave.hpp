#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/// C6 协处理器管理单例
///
/// start() 在后台 Task 中早期启动 SDIO 复位（约 1.5s），
/// 与主线程的其他初始化并行执行。
/// waitReady() 供 BLE/WiFi 模块等待协处理器就绪。
/// 内部使用 EventGroup，支持任意次重入和多 Task 并发等待。
class Slave
{
public:
	static constexpr EventBits_t kReadyBit = BIT0;

	static Slave& instance();

	/// 启动协处理器初始化（后台 Task，非阻塞）
	void start();

	/// 等待协处理器就绪（阻塞直到初始化完成）
	void waitReady();

	/// 协处理器是否已就绪
	bool isReady() const;

private:
	Slave() = default;
	~Slave() = default;
	Slave(const Slave&) = delete;
	Slave& operator=(const Slave&) = delete;

	EventGroupHandle_t m_evt{};
	bool m_started{};
};
