/**
 * @file inputHost.cpp
 * @brief 远程客户端 fd → hostId 映射表
 */

#include "inputHost.hpp"
#include <esp_log.h>
#include <cstring>

static constexpr char TAG[] = "InputHost";

// ════════════════════════════════════════════════════════════════
//  单例
// ════════════════════════════════════════════════════════════════

InputHost& InputHost::instance()
{
	static InputHost s_instance{};
	return s_instance;
}

// ════════════════════════════════════════════════════════════════
//  注册 / 注销
// ════════════════════════════════════════════════════════════════

int InputHost::registerClient(int fd, const char* hostname)
{
	// 先检查是否已注册（fd 复用情况）
	for (int i = 0; i < MAX_SLOTS; i++)
	{
		if (m_slots[i].occupied && m_slots[i].fd == fd)
		{
			ESP_LOGW(TAG, "fd %d already registered as hostId %d", fd, i + 1);
			return i + 1;
		}
	}

	// 找空闲槽位
	for (int i = 0; i < MAX_SLOTS; i++)
	{
		if (!m_slots[i].occupied)
		{
			m_slots[i].fd = fd;
			m_slots[i].occupied = true;
			if (hostname)
				strncpy(m_slots[i].hostname, hostname, sizeof(m_slots[i].hostname) - 1);
			else
				m_slots[i].hostname[0] = '\0';
			ESP_LOGI(TAG, "registered fd=%d -> hostId=%d (%s)", fd, i + 1, m_slots[i].hostname);
			return i + 1;  // hostId = index + 1
		}
	}

	ESP_LOGW(TAG, "no free slot for fd %d", fd);
	return -1;
}

void InputHost::unregisterClient(int fd)
{
	for (int i = 0; i < MAX_SLOTS; i++)
	{
		if (m_slots[i].occupied && m_slots[i].fd == fd)
		{
			ESP_LOGI(TAG, "unregistered fd=%d (was hostId=%d, %s)",
					 fd, i + 1, m_slots[i].hostname);
			m_slots[i] = Slot{};
			return;
		}
	}
	ESP_LOGW(TAG, "fd %d not found, nothing to unregister", fd);
}

// ════════════════════════════════════════════════════════════════
//  查询
// ════════════════════════════════════════════════════════════════

int InputHost::getHostIdByFd(int fd) const
{
	for (int i = 0; i < MAX_SLOTS; i++)
	{
		if (m_slots[i].occupied && m_slots[i].fd == fd)
			return i + 1;
	}
	return -1;
}

void InputHost::swapHostIds(int hostIdA, int hostIdB)
{
	int idxA = hostIdA - 1;
	int idxB = hostIdB - 1;
	if (idxA < 0 || idxA >= MAX_SLOTS || idxB < 0 || idxB >= MAX_SLOTS)
		return;

	Slot tmp = m_slots[idxA];
	m_slots[idxA] = m_slots[idxB];
	m_slots[idxB] = tmp;
	ESP_LOGI(TAG, "swapped hostId %d <-> %d", hostIdA, hostIdB);
}

bool InputHost::hasFreeSlot() const
{
	for (int i = 0; i < MAX_SLOTS; i++)
	{
		if (!m_slots[i].occupied)
			return true;
	}
	return false;
}
