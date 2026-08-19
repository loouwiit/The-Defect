#include "otaManager.hpp"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <nvs_flash.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

static constexpr char TAG[] = "ota";

// NVS 存储
static constexpr char NvsNamespace[] = "ota";
static constexpr char NvsKeyPendingSlot[] = "pending_slot";     // 升级中目标槽（重启后比对判断是否回滚）
static constexpr char NvsKeyLastRollback[] = "last_rollback";   // 最近一次回滚的槽
static constexpr char NvsKeyLastRollbackSec[] = "last_rb_sec";  // 最近一次回滚的 boot 秒数

// 镜像头中 esp_app_desc_t 的偏移 = image header + 第一段 segment header
static constexpr size_t AppDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
// 预检需要读到的镜像头大小（覆盖整个 app_desc）
static constexpr size_t HeaderReadSize = AppDescOffset + sizeof(esp_app_desc_t);

// ── 单例：Meyers' Singleton（C++11 magic static，线程安全构造）──
// 首次调用即构造：创建内部 RAM 常驻任务 + 请求队列。构造完成后所有方法可用，
// 不再需要显式 init / 互斥量防重复创建（语言保证仅构造一次）。
OtaManager& OtaManager::instance()
{
	static OtaManager inst;   // 函数局部 static → .bss（内部 RAM，栈数组要求）→ 常驻不析构
	return inst;
}

// ══ 内部 RAM 常驻任务 + 请求队列（P4 硬性要求，见头文件说明）══
// 静态资源而非每次 heap_caps_malloc：
//   1) 任务栈从堆分配 → 若溢出会踩坏堆相邻块（Bad tail/multi_heap 损坏）
//   2) 静态缓冲在内部 RAM，写 flash 禁 cache 窗口内访问安全
// 架构：常驻任务（ElfManager 同款已验证模式）等请求队列，避免"临时任务 + vTaskSuspend +
//       vTaskDelete"在 cache 暂停期交叉核访问已释放栈的竞态。
// 在构造函数中创建（instance() 首次调用，magic static 保证仅一次且线程安全）。
OtaManager::OtaManager()
{
	// 先建队列再建任务（worker 阻塞等队列，任务创建后立即安全）
	m_otaIntQueue = xQueueCreate(1, sizeof(InternalStackCtx));
	m_otaIntHandle = xTaskCreateStatic(internalStackWorker, "otaInt",
		sizeof(m_otaIntStack) / sizeof(StackType_t), this,
		tskIDLE_PRIORITY + 5, m_otaIntStack, &m_otaIntTcb);
	ESP_LOGI(TAG, "内部 RAM 栈任务已创建（otaInt）");
}

void OtaManager::internalStackWorker(void* p)
{
	auto* self = static_cast<OtaManager*>(p);
	InternalStackCtx req;
	while (true)
	{
		if (xQueueReceive(self->m_otaIntQueue, &req, portMAX_DELAY) != pdTRUE)
			continue;
		ESP_LOGI(TAG, "otaInt: 开始执行 fn");
		req.fn(req.arg);
		ESP_LOGI(TAG, "otaInt: fn 完成");
		xSemaphoreGive(req.done);
	}
}

void OtaManager::runInInternalStack(void (*fn)(void*), void* arg)
{
	// 请求上下文在调用者栈上（PSRAM）：worker 仅在 fn 之前（cache 使能期间）读取，安全
	InternalStackCtx ctx{ fn, arg, xSemaphoreCreateBinary() };
	xQueueSend(m_otaIntQueue, &ctx, portMAX_DELAY);
	xSemaphoreTake(ctx.done, portMAX_DELAY);
	vSemaphoreDelete(ctx.done);
}

/* 内部 RAM 静态写缓冲 + 镜像头缓冲（httpOta 经 getWriteBuffer/getHeaderBuffer 取用）：
   esp_ota_write 写 flash 期间禁用 cache，源缓冲必须在内部 RAM（PSRAM 依赖 cache = 崩）；
   放静态 .bss 而非堆/栈，彻底避免堆损坏与栈溢出。 */
static uint8_t s_otaWriteBuf[4096] __attribute__((aligned(4)));
static uint8_t s_otaHeaderBuf[OtaManager::HeaderSize] __attribute__((aligned(4)));

uint8_t* OtaManager::getWriteBuffer() { return s_otaWriteBuf; }
uint8_t* OtaManager::getHeaderBuffer() { return s_otaHeaderBuf; }

// ── 内部：NVS 辅助 ──

static void otaNvsSaveString(const char* key, const char* value)
{
	nvs_handle_t handle;
	if (nvs_open(NvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
	nvs_set_str(handle, key, value);
	nvs_commit(handle);
	nvs_close(handle);
}

static bool otaNvsLoadString(const char* key, char* buffer, size_t size)
{
	nvs_handle_t handle;
	if (nvs_open(NvsNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
	size_t len = size;
	esp_err_t err = nvs_get_str(handle, key, buffer, &len);
	nvs_close(handle);
	return err == ESP_OK;
}

static void otaNvsSaveI64(const char* key, int64_t value)
{
	nvs_handle_t handle;
	if (nvs_open(NvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
	nvs_set_i64(handle, key, value);
	nvs_commit(handle);
	nvs_close(handle);
}

static bool otaNvsLoadI64(const char* key, int64_t* value)
{
	nvs_handle_t handle;
	if (nvs_open(NvsNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
	esp_err_t err = nvs_get_i64(handle, key, value);
	nvs_close(handle);
	return err == ESP_OK;
}

static void otaNvsEraseKey(const char* key)
{
	nvs_handle_t handle;
	if (nvs_open(NvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return;
	nvs_erase_key(handle, key);
	nvs_commit(handle);
	nvs_close(handle);
}

// ── 状态名映射（JSON 用）──

static const char* otaStateName(esp_ota_img_states_t state)
{
	switch (state)
	{
	case ESP_OTA_IMG_UNDEFINED:     return "undefined";
	case ESP_OTA_IMG_VALID:         return "valid";
	case ESP_OTA_IMG_INVALID:       return "invalid";
	case ESP_OTA_IMG_ABORTED:       return "aborted";
	case ESP_OTA_IMG_NEW:           return "new";
	case ESP_OTA_IMG_PENDING_VERIFY:return "pending_verify";
	default:                        return "unknown";
	}
}

// 读槽位版本（无有效镜像返回空串）
static void otaSlotVersion(const esp_partition_t* part, char* out, size_t size)
{
	out[0] = '\0';
	if (part == nullptr) return;
	esp_app_desc_t desc;
	if (esp_ota_get_partition_description(part, &desc) == ESP_OK)
	{
		strncpy(out, desc.version, size - 1);
		out[size - 1] = '\0';
	}
}

// ── 启动早期 ──

void OtaManager::checkRollback()
{
	const esp_partition_t* running = esp_ota_get_running_partition();
	m_otaResult = {};   // 每次启动重置

	// 填充运行中槽位信息
	if (running != nullptr)
	{
		snprintf(m_otaResult.slot, sizeof(m_otaResult.slot), "%s", running->label);
		esp_app_desc_t desc;
		if (esp_ota_get_partition_description(running, &desc) == ESP_OK)
			snprintf(m_otaResult.version, sizeof(m_otaResult.version), "%s", desc.version);
	}

	// 检查当前固件状态（pendingVerify 独立于 OTA 事件，每次启动都查）
	if (running != nullptr)
	{
		esp_ota_img_states_t state;
		if (esp_ota_get_state_partition(running, &state) == ESP_OK)
		{
			m_otaResult.pendingVerify = (state == ESP_OTA_IMG_PENDING_VERIFY);
			const char* hint = m_otaResult.pendingVerify ? "（未确认则重启回退）" : "";
			ESP_LOGI(TAG, "当前固件 %s 状态: %s%s", running->label, otaStateName(state), hint);
		}
	}

	// 比对 pending 槽与 running：判断上次升级是否回滚
	char pendingSlot[32] = "";
	if (otaNvsLoadString(NvsKeyPendingSlot, pendingSlot, sizeof(pendingSlot)))
	{
		m_otaResult.hasEvent = true;
		snprintf(m_otaResult.pendingSlot, sizeof(m_otaResult.pendingSlot), "%s", pendingSlot);

		if (running != nullptr && strcmp(pendingSlot, running->label) != 0)
		{
			// 目标槽没跑起来 → 回滚发生
			int64_t bootSec = esp_timer_get_time() / 1000000;
			otaNvsSaveString(NvsKeyLastRollback, pendingSlot);
			otaNvsSaveI64(NvsKeyLastRollbackSec, bootSec);
			m_otaResult.rolledBack = true;
			ESP_LOGW(TAG, "上次 OTA 已回滚: %s 无效, 当前运行 %s", pendingSlot, running->label);
		}
		else
		{
			ESP_LOGI(TAG, "OTA 升级成功, 当前运行 %s", running ? running->label : "?");
		}
		otaNvsEraseKey(NvsKeyPendingSlot);
	}
}

// ── 手动确认当前固件（关闭回滚窗口）──

esp_err_t OtaManager::confirmRunning()
{
	esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
	if (err != ESP_OK)
		ESP_LOGE(TAG, "确认当前固件失败: %s", esp_err_to_name(err));
	else
		ESP_LOGI(TAG, "当前固件已手动标记 VALID（回滚窗口关闭）");
	return err;
}

// ── OTA 并发标志 ──

bool OtaManager::isUpdating()
{
	return m_otaUpdating;
}

void OtaManager::setUpdating(bool updating)
{
	m_otaUpdating = updating;
}

// ── 升级流程 ──

const void* OtaManager::getNextUpdatePartition()
{
	return esp_ota_get_next_update_partition(nullptr);
}

size_t OtaManager::getUpdatePartitionSize()
{
	const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
	return part ? part->size : 0;
}

esp_err_t OtaManager::precheckImage(const uint8_t* header, size_t headerLen)
{
	// 头不够覆盖整个 app_desc → 无法预检，拒绝
	if (header == nullptr || headerLen < HeaderReadSize)
		return ESP_ERR_INVALID_ARG;

	// magic 校验（非有效 ESP 镜像）
	if (header[0] != ESP_IMAGE_HEADER_MAGIC)
		return ESP_ERR_IMAGE_INVALID;

	// 解析 app_desc（在固定偏移处）
	const esp_app_desc_t* desc = (const esp_app_desc_t*)(header + AppDescOffset);
	if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD)
		return ESP_ERR_IMAGE_INVALID;

	// 版本防重烧：与 last_invalid 同版本 → 拒绝
	const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
	if (invalid != nullptr)
	{
		esp_app_desc_t invalidDesc;
		if (esp_ota_get_partition_description(invalid, &invalidDesc) == ESP_OK)
		{
			if (strncmp(desc->version, invalidDesc.version, sizeof(desc->version)) == 0)
			{
				ESP_LOGW(TAG, "拒绝上传: 版本 %s 与上次失败版本相同", desc->version);
				return ESP_ERR_OTA_PARTITION_CONFLICT;
			}
		}
	}

	ESP_LOGI(TAG, "镜像预检通过, 版本 %s", desc->version);
	return ESP_OK;
}

esp_err_t OtaManager::begin(void** outHandle)
{
	if (outHandle == nullptr)
		return ESP_ERR_INVALID_ARG;

	if (m_updatePartition == nullptr)
	{
		m_updatePartition = esp_ota_get_next_update_partition(nullptr);
		if (m_updatePartition == nullptr)
		{
			ESP_LOGE(TAG, "无可用 OTA 分区");
			return ESP_ERR_NOT_FOUND;
		}
	}

	esp_ota_handle_t handle = 0;
	esp_err_t err = esp_ota_begin(m_updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
		return err;
	}

	// 记录目标槽 pending（重启后用于判定是否回滚）
	otaNvsSaveString(NvsKeyPendingSlot, m_updatePartition->label);
	ESP_LOGI(TAG, "OTA 开始写入 %s (size %u)", m_updatePartition->label, (unsigned)m_updatePartition->size);

	*outHandle = (void*)(uintptr_t)handle;
	return ESP_OK;
}

esp_err_t OtaManager::write(void* handle, const uint8_t* data, size_t len)
{
	return esp_ota_write((esp_ota_handle_t)(uintptr_t)handle, data, len);
}

void OtaManager::abort(void* handle)
{
	esp_ota_abort((esp_ota_handle_t)(uintptr_t)handle);
	otaNvsEraseKey(NvsKeyPendingSlot);
	ESP_LOGW(TAG, "OTA 已中止");
}

esp_err_t OtaManager::finish(void* handle)
{
	// esp_ota_end 校验整镜像（SHA256）
	esp_err_t err = esp_ota_end((esp_ota_handle_t)(uintptr_t)handle);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_ota_end 校验失败: %s", esp_err_to_name(err));
		// 校验失败必须清除 pending，防止重启后误报为回滚事件
		otaNvsEraseKey(NvsKeyPendingSlot);
		return err;
	}

	// 设置下次启动分区
	err = esp_ota_set_boot_partition(m_updatePartition);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败: %s", esp_err_to_name(err));
	else
		ESP_LOGI(TAG, "OTA 完成, 下次启动 %s", m_updatePartition->label);
	return err;
}

// ── 手动分区切换 / 回退 ──

esp_err_t OtaManager::switchBootPartition(const char* slotLabel, bool invalidateCurrent)
{
	// 目标槽校验（仅允许 ota_0..ota_15，按 label 匹配）
	const esp_partition_t* target = nullptr;
	for (int t = ESP_PARTITION_SUBTYPE_APP_OTA_0; t < ESP_PARTITION_SUBTYPE_APP_OTA_MAX; t++)
	{
		const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, (esp_partition_subtype_t)t, nullptr);
		if (part != nullptr && slotLabel != nullptr && strcmp(part->label, slotLabel) == 0)
		{
			target = part;
			break;
		}
	}
	if (target == nullptr)
	{
		ESP_LOGW(TAG, "切换失败: 无此分区 %s", slotLabel ? slotLabel : "(null)");
		return ESP_ERR_NOT_FOUND;
	}

	// 非当前运行分区
	const esp_partition_t* running = esp_ota_get_running_partition();
	if (running != nullptr && target == running)
	{
		ESP_LOGW(TAG, "切换失败: %s 已是当前运行分区", target->label);
		return ESP_ERR_INVALID_STATE;
	}

	// 目标槽必须有有效镜像（防切到空槽/坏槽后无法启动）
	esp_app_desc_t desc;
	if (esp_ota_get_partition_description(target, &desc) != ESP_OK)
	{
		ESP_LOGW(TAG, "切换失败: %s 无有效镜像", target->label);
		return ESP_ERR_IMAGE_INVALID;
	}

	// 可选：标记当前分区 INVALID（手动回退语义）。
	// mark 失败（如另一槽无有效 otadata 记录）不阻塞切换，仅警告。
	if (invalidateCurrent)
	{
		esp_err_t e = esp_ota_mark_app_invalid_rollback();
		if (e != ESP_OK)
			ESP_LOGW(TAG, "标记当前分区无效失败(%s)，仍执行切换", esp_err_to_name(e));
		else
			ESP_LOGW(TAG, "当前分区 %s 已标记 INVALID", running ? running->label : "?");
	}

	// 设置下次启动分区（内部会把目标槽状态重置为可启动，含之前 ABORTED/INVALID 的槽）
	esp_err_t err = esp_ota_set_boot_partition(target);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "esp_ota_set_boot_partition(%s) 失败: %s", target->label, esp_err_to_name(err));
		return err;
	}
	ESP_LOGI(TAG, "下次启动分区: %s%s", target->label,
		invalidateCurrent ? "（当前已标记无效）" : "");
	return ESP_OK;
}

// ── /api/otaInfo JSON ──

void OtaManager::getInfoJson(char* buffer, size_t size)
{
	if (buffer == nullptr || size == 0) return;
	size_t n = 0;

	auto append = [&](const char* fmt, ...) {
		if (n >= size) return;
		va_list args;
		va_start(args, fmt);
		int written = vsnprintf(buffer + n, size - n, fmt, args);
		va_end(args);
		if (written > 0) n += written;
	};

	const esp_partition_t* running = esp_ota_get_running_partition();
	const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
	const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();

	char ver[33];
	append("{");

	// running
	otaSlotVersion(running, ver, sizeof(ver));
	const char* runState = "unknown";
	if (running != nullptr)
	{
		esp_ota_img_states_t st;
		if (esp_ota_get_state_partition(running, &st) == ESP_OK)
			runState = otaStateName(st);
	}
	append("\"running\":{\"label\":\"%s\",\"version\":\"%s\",\"state\":\"%s\"},",
		running ? running->label : "", ver, runState);

	// next
	otaSlotVersion(next, ver, sizeof(ver));
	append("\"next\":{\"label\":\"%s\",\"version\":\"%s\"},",
		next ? next->label : "", ver);

	// last_invalid
	otaSlotVersion(invalid, ver, sizeof(ver));
	append("\"last_invalid\":%s",
		invalid ? "{" : "null");
	if (invalid)
		append("\"label\":\"%s\",\"version\":\"%s\"}", invalid->label, ver);
	append(",");

	// last_rollback（NVS 记录）
	{
		char slot[32] = "";
		int64_t bootSec = 0;
		if (otaNvsLoadString(NvsKeyLastRollback, slot, sizeof(slot)) && otaNvsLoadI64(NvsKeyLastRollbackSec, &bootSec))
			append("\"last_rollback\":{\"slot\":\"%s\",\"boot_sec\":%lld},", slot, (long long)bootSec);
		else
			append("\"last_rollback\":null,");
	}

	// slots 状态（遍历 ota_0..ota_1）
	append("\"slots\":[");
	bool first = true;
	for (int t = ESP_PARTITION_SUBTYPE_APP_OTA_0; t < ESP_PARTITION_SUBTYPE_APP_OTA_MAX; t++)
	{
		const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, (esp_partition_subtype_t)t, nullptr);
		if (part == nullptr) continue;
		esp_ota_img_states_t state;
		const char* stateName = "empty";
		if (esp_ota_get_state_partition(part, &state) == ESP_OK)
			stateName = otaStateName(state);
		append("%s{\"label\":\"%s\",\"state\":\"%s\"}", first ? "" : ",", part->label, stateName);
		first = false;
	}
	append("]}");
}
