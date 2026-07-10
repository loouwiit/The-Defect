/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 /**
  * @brief LP Core 固件 — 触控唤醒检测
  *
  * 工作方式:
  *   LP 定时器定期 (10ms) 唤醒 LP Core
  *   LP Core 通过 LP ADC 读取 GPIO21 (ADC1_CH5) 电压
  *   若电压低于阈值 (触控 INT 拉低), 唤醒主 CPU
  *   否则继续 Deep-sleep
  *
  * 由 PowerManager 在关机前加载并启动此固件。
  *
  * @note  ulp_lp_debug_counter 位于 LP SRAM，HP 侧可通过
  *        PowerManager 读取，以验证 LP Core 是否在运行。
  */

#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_lp_adc_shared.h"

  //GT911 INT 引脚 — GPIO21 = ADC1_CH5
#define TOUCH_ADC_UNIT      ADC_UNIT_1
#define TOUCH_ADC_CHANNEL   ADC_CHANNEL_5

#define TOUCH_THRESHOLD  2500

/* ════════════════════════════════════════════════════════════════
 *  调试共享变量（位于 LP SRAM，Deep-sleep 保留，HP 侧可读取）
 * ════════════════════════════════════════════════════════════════ */

 /**
  * ═══════════════════════════════════════════════════
  *  调试共享变量
  *  注意: 变量名不含 ulp_ 前缀，构建系统自动添加
  *  HP 侧通过 #include "ulp.h" 访问这些变量
  * ═══════════════════════════════════════════════════
  */

  /** LP Core 运行次数计数器 */
uint32_t lp_debug_counter __attribute__((section(".lp_ram")));

/** 最后一次 ADC 原始值 */
uint32_t lp_last_adc __attribute__((section(".lp_ram")));

/** 观察到的 ADC 最小值 */
uint32_t lp_min_adc __attribute__((section(".lp_ram")));

/** 观察到的 ADC 最大值 */
uint32_t lp_max_adc __attribute__((section(".lp_ram")));

/**
 * @brief 触摸检测结果标志
 *
 * 由 LP Core 设置，HP CPU 读取后清零。
 *   - 0: 未检测到触摸
 *   - 1: 检测到触摸，请求唤醒
 */
uint32_t lp_touch_detected __attribute__((section(".lp_ram")));

/** LP Core 尝试唤醒 HP CPU 的次数 */
uint32_t lp_wakeup_count __attribute__((section(".lp_ram")));

void main(void)
{
	int value = 0;

	/* 每次 LP Core 被唤醒时递增计数器 */
	lp_debug_counter++;

	/* 先清零触摸标志（仅在本次检测到触摸时设为 1） */
	lp_touch_detected = 0;

	/* 读取 LP ADC 电压 */
	esp_err_t ret = lp_core_lp_adc_read_channel_converted(TOUCH_ADC_UNIT, TOUCH_ADC_CHANNEL, &value);

	if (ret != ESP_OK) {
		lp_last_adc = 0xDEAD;
		return;
	}

	/* 记录当前 ADC值 */
	lp_last_adc = (uint32_t)value;

	/* 更新最小/最大值 */
	if (value < (int)lp_min_adc || lp_min_adc == 0) {
		lp_min_adc = (uint32_t)value;
	}
	if (value > (int)lp_max_adc) {
		lp_max_adc = (uint32_t)value;
	}

	/* 低于阈值 = GT911 INT 拉低 = 触摸屏幕 */
	if (value < TOUCH_THRESHOLD) {
		lp_touch_detected = 1;
	}

	/* 未检测到触控，继续 Deep-sleep */
	/* LP Core 从这里返回 → LP 定时器下次到期时再次唤醒 LP Core 执行 main() */
}
