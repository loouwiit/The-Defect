#pragma once

#include <cstdint>

/**
 * @brief BLE 协议栈管理器
 *
 * 单例。负责 NimBLE Host 初始化、GATT Server 注册、广播启动。
 * 蓝牙 Controller 由 C6 通过 SDIO (esp-hosted) 提供。
 *
 * 初始化顺序（参考 ESP-IDF bleprph 示例）:
 *   1. esp_nimble_init()          — 初始化 NimBLE Host
 *   2. ble_hs_cfg.sync_cb 注册    — GATT Server + 广播
 *   3. nimble_port_freertos_init() — 创建 Host 任务
 */
class Ble
{
public:
	static Ble& instance();

	bool start();
	void stop();

	bool isStarted() const { return started; }

private:
	Ble() = default;
	~Ble() = default;

	bool started = false;

	static Ble* s_instance;
	static constexpr char TAG[] = "Ble";

	// NimBLE 回调
	static void syncCb(void);
	static void resetCb(int reason);
	static int gapEventCb(struct ble_gap_event* event, void* arg);
	static void hostTask(void* param);
	static void advStart();
};
