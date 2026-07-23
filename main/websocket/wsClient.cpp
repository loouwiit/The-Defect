/**
 * @file wsClient.cpp
 * @brief 通用 WebSocket 客户端 — 薄封装 esp_websocket_client
 */

#include "wsClient.hpp"
#include <esp_log.h>
#include <esp_websocket_client.h>
#include <cstring>

static constexpr char TAG[] = "WsClient";

// ============================================================
//  构造 / 析构
// ============================================================

WsClient::WsClient() = default;

WsClient::~WsClient()
{
	disconnect();
}

// ============================================================
//  连接 / 断开
// ============================================================

bool WsClient::connect(const char* host, uint16_t port, const char* uri)
{
	if (m_client)
	{
		ESP_LOGW(TAG, "already connected, disconnect first");
		return false;
	}

	// 构建 ws://host:port/uri
	char uriBuf[256];
	int n = snprintf(uriBuf, sizeof(uriBuf), "ws://%s:%u%s", host, port, uri);
	if (n <= 0 || n >= (int)sizeof(uriBuf))
	{
		ESP_LOGE(TAG, "URI too long");
		return false;
	}

	ESP_LOGI(TAG, "connecting to %s ...", uriBuf);

	esp_websocket_client_config_t cfg = {};
	cfg.uri = uriBuf;
	cfg.buffer_size = 2048;
	cfg.task_stack = 4096;
	cfg.reconnect_timeout_ms = 5000;
	cfg.network_timeout_ms = 10000;
	cfg.disable_auto_reconnect = true;

	m_client = esp_websocket_client_init(&cfg);
	if (!m_client)
	{
		ESP_LOGE(TAG, "ws client init failed");
		return false;
	}

	esp_err_t ret = esp_websocket_register_events(
		m_client, WEBSOCKET_EVENT_ANY, eventHandler, this);
	if (ret != ESP_OK)
	{
		esp_websocket_client_destroy(m_client);
		m_client = nullptr;
		return false;
	}

	ret = esp_websocket_client_start(m_client);
	if (ret != ESP_OK)
	{
		esp_websocket_client_destroy(m_client);
		m_client = nullptr;
		return false;
	}

	return true;
}

void WsClient::disconnect()
{
	if (!m_client) return;

	esp_websocket_client_stop(m_client);
	esp_websocket_client_destroy(m_client);
	m_client = nullptr;

	ESP_LOGI(TAG, "disconnected");
}

bool WsClient::isConnected() const
{
	return m_client && esp_websocket_client_is_connected(m_client);
}

// ============================================================
//  发送
// ============================================================

bool WsClient::sendText(const char* data, size_t len)
{
	if (!isConnected())
	{
		ESP_LOGW(TAG, "not connected, cannot send");
		return false;
	}

	esp_err_t ret = esp_websocket_client_send_text(
		m_client, data, len, pdMS_TO_TICKS(1000));
	return ret == ESP_OK;
}

bool WsClient::sendBinary(const uint8_t* data, size_t len)
{
	if (!isConnected())
	{
		ESP_LOGW(TAG, "not connected, cannot send");
		return false;
	}

	// esp_websocket_client_send_bin 接受 const char*，底层数据不变
	esp_err_t ret = esp_websocket_client_send_bin(
		m_client, reinterpret_cast<const char*>(data), len, pdMS_TO_TICKS(1000));
	return ret == ESP_OK;
}

// ============================================================
//  事件处理
// ============================================================

void WsClient::eventHandler(void* arg, esp_event_base_t base,
	int32_t eventId, void* eventData)
{
	auto* self = static_cast<WsClient*>(arg);
	auto* data = static_cast<esp_websocket_event_data_t*>(eventData);
	self->handleEvent(eventId, data);
}

void WsClient::handleEvent(int32_t eventId, esp_websocket_event_data_t* data)
{
	switch (eventId)
	{
	case WEBSOCKET_EVENT_CONNECTED:
		ESP_LOGI(TAG, "connected");
		if (m_onConnected) m_onConnected();
		break;

	case WEBSOCKET_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "disconnected");
		if (m_onDisconnected) m_onDisconnected();
		break;

	case WEBSOCKET_EVENT_DATA:
		if (data->op_code == 1 && data->data_len > 0 && m_onMessage)
		{
			m_onMessage(static_cast<const char*>(data->data_ptr), data->data_len);
		}
		break;

	case WEBSOCKET_EVENT_ERROR:
		ESP_LOGW(TAG, "error");
		break;

	case WEBSOCKET_EVENT_CLOSED:
		ESP_LOGI(TAG, "closed");
		break;

	default:
		break;
	}
}
