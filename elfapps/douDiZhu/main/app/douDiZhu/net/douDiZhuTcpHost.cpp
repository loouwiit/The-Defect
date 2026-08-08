#include "douDiZhuTcpHost.hpp"

#include <cstring>
#include <cstdio>
#include <cJSON.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include "douDiZhuProtocol.hpp"

static constexpr char TAG[] = "DouDizhuHost";

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

DouDizhuTcpHost::~DouDizhuTcpHost()
{
	stop();
}

void DouDizhuTcpHost::start(const Callbacks& cb)
{
	m_cb = cb;
	m_started = true;
	ESP_LOGI(TAG, "host started");
}

void DouDizhuTcpHost::stop()
{
	m_started = false;
	for (auto& c : m_conns) closeConn(c);
	vTaskDelay(pdMS_TO_TICKS(80));
}

// ════════════════════════════════════════════════════════════════
//  连接管理
// ════════════════════════════════════════════════════════════════

bool DouDizhuTcpHost::connectToClient(const char* ip, uint16_t port, int seat)
{
	if (seat < 1 || seat > MAX_CLIENTS) return false;
	int idx = seat - 1;

	Lock lock(m_mutex);
	if (m_conns[idx].active) return true;   // 已连接

	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) return false;

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &addr.sin_addr);

	if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		ESP_LOGW(TAG, "connect to seat%d %s:%d failed: %d", seat, ip, port, errno);
		close(sock);
		return false;
	}
	int flag = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	auto& c = m_conns[idx];
	c = Conn{};
	c.sock = sock;
	c.seat = seat;
	c.active = true;

	auto* arg = new TaskArg{ this, &c };
	xTaskCreate(recvTaskFn, "ddzHostRecv", 4096, arg, tskIDLE_PRIORITY + 3, &c.recvTask);
	ESP_LOGI(TAG, "connected to seat%d %s:%d", seat, ip, port);
	return true;
}

void DouDizhuTcpHost::closeConn(Conn& c)
{
	{
		Lock lock(m_mutex);
		if (!c.active) return;
		c.active = false;
		if (c.sock >= 0) {
			shutdown(c.sock, SHUT_RDWR);
			close(c.sock);
			c.sock = -1;
		}
	}
	if (c.recvTask) {
		vTaskDelay(pdMS_TO_TICKS(40));
		c.recvTask = nullptr;
	}
}

bool DouDizhuTcpHost::isSeatConnected(int seat) const
{
	if (seat < 1 || seat > MAX_CLIENTS) return false;
	Lock lock(m_mutex);
	return m_conns[seat - 1].active;
}

// ════════════════════════════════════════════════════════════════
//  发送
// ════════════════════════════════════════════════════════════════

static void sendJson(int sock, const char* jsonStr)
{
	if (sock < 0 || !jsonStr) return;
	char buf[2048];
	int len = snprintf(buf, sizeof(buf), "%s\n", jsonStr);
	send(sock, buf, len, 0);
}

void DouDizhuTcpHost::sendToSeat(int seat, const char* json)
{
	if (seat < 1 || seat > MAX_CLIENTS) return;
	Lock lock(m_mutex);
	auto& c = m_conns[seat - 1];
	if (c.active) sendJson(c.sock, json);
}

void DouDizhuTcpHost::broadcast(const char* json)
{
	Lock lock(m_mutex);
	for (auto& c : m_conns)
		if (c.active) sendJson(c.sock, json);
}

// ════════════════════════════════════════════════════════════════
//  接收 Task — 按 \n 分割 JSON
// ════════════════════════════════════════════════════════════════

void DouDizhuTcpHost::recvTaskFn(void* arg)
{
	auto* ta = static_cast<TaskArg*>(arg);
	DouDizhuTcpHost* self = ta->self;
	Conn& c = *ta->conn;
	delete ta;

	char buf[128];
	while (c.active && c.sock >= 0) {
		int n = recv(c.sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) {
			if (n == 0) ESP_LOGI(TAG, "seat%d disconnected", c.seat);
			else ESP_LOGW(TAG, "seat%d recv error %d", c.seat, n);
			break;
		}
		for (int i = 0; i < n; i++) {
			char ch = buf[i];
			if (ch == '\n') {
				c.lineBuf[c.lineLen] = '\0';
				if (c.lineLen > 0) self->processLine(c, c.lineBuf);
				c.lineLen = 0;
			} else if (c.lineLen < sizeof(c.lineBuf) - 1) {
				c.lineBuf[c.lineLen++] = ch;
			}
		}
	}

	int seat = c.seat;
	self->closeConn(c);
	if (self->m_cb.onDisconnect) self->m_cb.onDisconnect(seat);
	vTaskDelete(nullptr);
}

void DouDizhuTcpHost::processLine(Conn& c, const char* line)
{
	cJSON* root = cJSON_Parse(line);
	if (!root) return;
	cJSON* t = cJSON_GetObjectItem(root, "type");
	if (!t || !cJSON_IsString(t)) { cJSON_Delete(root); return; }
	const char* tag = t->valuestring;

	if (strcmp(tag, DouDizhuMsg::BID) == 0) {
		cJSON* v = cJSON_GetObjectItem(root, "value");
		int value = (v && cJSON_IsNumber(v)) ? v->valueint : 0;
		if (m_cb.onBid) m_cb.onBid(c.seat, value);
	}
	else if (strcmp(tag, DouDizhuMsg::PLAY) == 0) {
		cJSON* pass = cJSON_GetObjectItem(root, "pass");
		if (pass && cJSON_IsTrue(pass)) {
			if (m_cb.onPass) m_cb.onPass(c.seat);
		} else {
			cJSON* cards = cJSON_GetObjectItem(root, "cards");
			uint8_t buf[DouDizhuLogic::MAX_HAND];
			int n = 0;
			if (cards && cJSON_IsArray(cards)) {
				int sz = cJSON_GetArraySize(cards);
				if (sz > DouDizhuLogic::MAX_HAND) sz = DouDizhuLogic::MAX_HAND;
				for (int i = 0; i < sz; i++) {
					cJSON* item = cJSON_GetArrayItem(cards, i);
					if (item && cJSON_IsNumber(item)) buf[n++] = (uint8_t)item->valueint;
				}
			}
			if (m_cb.onPlay) m_cb.onPlay(c.seat, buf, n);
		}
	}
	else if (strcmp(tag, DouDizhuMsg::REMATCH) == 0) {
		if (m_cb.onRematch) m_cb.onRematch(c.seat);
	}

	cJSON_Delete(root);
}
