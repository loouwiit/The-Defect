#pragma once

#include <cstdint>

/**
 * @brief BLE 协议栈管理器（Central 模式）
 *
 * 单例。P4 作为 BLE Central，扫描并连接游戏手柄（Peripheral）。
 * 蓝牙 Controller 由 C6 通过 SDIO (esp-hosted) 提供。
 *
 * 流程:
 *   1. NimBLE 初始化
 *   2. sync 后自动扫描
 *   3. 发现手柄 → 连接 → GATT 发现 → 订阅通知 → 接收数据
 */
class Ble
{
public:
	static Ble& instance();

	bool start();
	void stop();

	/** 开始扫描游戏手柄 */
	void startScan();

	bool isStarted() const { return started; }

private:
	Ble() = default;
	~Ble() = default;

	bool started = false;
	bool scanning = false;

	static Ble* s_instance;
	static constexpr char TAG[] = "Ble";

	// NimBLE 回调
	static void syncCb(void);
	static void resetCb(int reason);
	static void hostTask(void* param);

public:
	/* GAP 事件回调（被 discCb/Central 流程调用） */
	static int gapEventCb(struct ble_gap_event* event, void* arg);
};
