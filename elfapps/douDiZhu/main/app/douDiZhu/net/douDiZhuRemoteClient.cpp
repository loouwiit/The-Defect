#include "douDiZhuRemoteClient.hpp"

#include <cstring>
#include <cstdio>
#include <cJSON.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

static constexpr char TAG[] = "DouDizhuClient";

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

DouDizhuRemoteClient::DouDizhuRemoteClient() = default;

DouDizhuRemoteClient::~DouDizhuRemoteClient()
{
	disconnect();
}

// ════════════════════════════════════════════════════════════════
//  连接 / 断开
// ════════════════════════════════════════════════════════════════

bool DouDizhuRemoteClient::listen(int port)
{
	int listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSock < 0) {
		ESP_LOGE(TAG, "socket create failed: %d", errno);
		return false;
	}
	int opt = 1;
	setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		ESP_LOGE(TAG, "bind port %d failed: %d", port, errno);
		close(listenSock);
		return false;
	}
	if (::listen(listenSock, 2) < 0) {
		ESP_LOGE(TAG, "listen failed: %d", errno);
		close(listenSock);
		return false;
	}
	ESP_LOGI(TAG, "listening on port %d, waiting for host...", port);

	struct sockaddr_in peer;
	socklen_t peerLen = sizeof(peer);
	m_sock = accept(listenSock, (struct sockaddr*)&peer, &peerLen);
	close(listenSock);

	if (m_sock < 0) {
		ESP_LOGE(TAG, "accept failed: %d", errno);
		return false;
	}
	int flag = 1;
	setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

	char peerStr[16];
	inet_ntop(AF_INET, &peer.sin_addr, peerStr, sizeof(peerStr));
	ESP_LOGI(TAG, "accepted connection from host %s", peerStr);

	m_running = true;
	xTaskCreate(recvTaskFn, "ddzClientRecv", 4096, this, tskIDLE_PRIORITY + 3, &m_recvTask);
	return true;
}

void DouDizhuRemoteClient::disconnect()
{
	m_running = false;
	if (m_sock >= 0) {
		shutdown(m_sock, SHUT_RDWR);
		close(m_sock);
		m_sock = -1;
	}
	if (m_recvTask) {
		vTaskDelay(pdMS_TO_TICKS(50));
		m_recvTask = nullptr;
	}
	{
		Lock lock(m_mutex);
		m_hasNew = false;
		m_view = DouDizhuClientView{};
	}
}

// ════════════════════════════════════════════════════════════════
//  状态访问
// ════════════════════════════════════════════════════════════════

int DouDizhuRemoteClient::getSeat() const
{
	Lock lock(m_mutex);
	return m_view.seat;
}

bool DouDizhuRemoteClient::hasNewState() const
{
	Lock lock(m_mutex);
	return m_hasNew;
}

void DouDizhuRemoteClient::consumeNewState()
{
	Lock lock(m_mutex);
	m_hasNew = false;
}

DouDizhuClientView DouDizhuRemoteClient::getView() const
{
	Lock lock(m_mutex);
	return m_view;
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

void DouDizhuRemoteClient::sendBid(int value)
{
	int sock = m_sock;
	if (sock < 0) return;
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::BID);
	cJSON_AddNumberToObject(root, "value", value);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { sendJson(sock, str); cJSON_free(str); }
}

void DouDizhuRemoteClient::sendPlay(const uint8_t* cards, int count)
{
	int sock = m_sock;
	if (sock < 0) return;
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::PLAY);
	cJSON* arr = cJSON_CreateArray();
	for (int i = 0; i < count; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(cards[i]));
	cJSON_AddItemToObject(root, "cards", arr);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { sendJson(sock, str); cJSON_free(str); }
}

void DouDizhuRemoteClient::sendPass()
{
	int sock = m_sock;
	if (sock < 0) return;
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::PLAY);
	cJSON_AddBoolToObject(root, "pass", true);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { sendJson(sock, str); cJSON_free(str); }
}

void DouDizhuRemoteClient::sendRematch()
{
	int sock = m_sock;
	if (sock < 0) return;
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::REMATCH);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { sendJson(sock, str); cJSON_free(str); }
}

// ════════════════════════════════════════════════════════════════
//  接收 Task
// ════════════════════════════════════════════════════════════════

void DouDizhuRemoteClient::recvTaskFn(void* arg)
{
	auto* self = static_cast<DouDizhuRemoteClient*>(arg);
	char buf[256];
	while (self->m_running && self->m_sock >= 0) {
		int n = recv(self->m_sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) {
			if (n == 0) ESP_LOGI(TAG, "connection closed by host");
			else ESP_LOGW(TAG, "recv error: %d", n);
			break;
		}
		self->processRecvData(buf, (size_t)n);
	}
	self->m_running = false;
	if (self->m_sock >= 0) { close(self->m_sock); self->m_sock = -1; }
	{
		Lock lock(self->m_mutex);
		self->m_view.seat = -1;
	}
	ESP_LOGW(TAG, "disconnected");
	if (self->m_onDisconnected) self->m_onDisconnected();
	self->m_recvTask = nullptr;
	vTaskDelete(nullptr);
}

void DouDizhuRemoteClient::processRecvData(const char* data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		char ch = data[i];
		if (ch == '\n') {
			m_lineBuf[m_lineLen] = '\0';
			if (m_lineLen > 0) processLine(m_lineBuf);
			m_lineLen = 0;
		} else if (m_lineLen < LINE_BUF_SIZE - 1) {
			m_lineBuf[m_lineLen++] = ch;
		}
	}
}

void DouDizhuRemoteClient::processLine(const char* line)
{
	cJSON* root = cJSON_Parse(line);
	if (!root) { ESP_LOGW(TAG, "JSON parse failed"); return; }
	cJSON* t = cJSON_GetObjectItem(root, "type");
	if (!t || !cJSON_IsString(t)) { cJSON_Delete(root); return; }
	const char* tag = t->valuestring;

	Lock lock(m_mutex);
	auto& v = m_view;

	if (strcmp(tag, DouDizhuMsg::WELCOME) == 0) {
		cJSON* s = cJSON_GetObjectItem(root, "seat");
		if (s && cJSON_IsNumber(s)) v.seat = s->valueint;
		cJSON* pc = cJSON_GetObjectItem(root, "player_count");
		if (pc && cJSON_IsNumber(pc)) v.playerCount = pc->valueint;
		ESP_LOGI(TAG, "welcome: seat=%d count=%d", v.seat, v.playerCount);
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::GAME_START) == 0) {
		cJSON* d = cJSON_GetObjectItem(root, "dealer");
		v.dealer = (d && cJSON_IsNumber(d)) ? d->valueint : 0;
		v.phase = 1;
		v.bidTurn = v.dealer;
		v.minBid = 1;
		v.maxBid = 0;
		v.maxBidder = -1;
		v.bidValue = 0;
		v.landlord = -1;
		v.handCount = 0;
		v.turn = -1;
		v.lastPlaySeat = -1;
		v.bottomRevealed = false;
		v.multiplier = 1;
		for (int i = 0; i < 3; i++) { v.trickCount[i] = 0; v.trickPass[i] = false; v.cardCount[i] = 17; }
		ESP_LOGI(TAG, "game start, dealer=%d", v.dealer);
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::DEAL) == 0) {
		cJSON* cards = cJSON_GetObjectItem(root, "cards");
		int n = 0;
		if (cards && cJSON_IsArray(cards)) {
			int sz = cJSON_GetArraySize(cards);
			if (sz > DouDizhuLogic::MAX_HAND) sz = DouDizhuLogic::MAX_HAND;
			for (int i = 0; i < sz; i++) {
				cJSON* item = cJSON_GetArrayItem(cards, i);
				if (item && cJSON_IsNumber(item)) v.hand[n++] = (uint8_t)item->valueint;
			}
		}
		v.handCount = n;
		DouDizhuLogic::sortHand(v.hand, v.handCount);
		ESP_LOGI(TAG, "deal: %d cards", v.handCount);
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::BID_TURN) == 0) {
		cJSON* s = cJSON_GetObjectItem(root, "seat");
		v.bidTurn = (s && cJSON_IsNumber(s)) ? s->valueint : -1;
		cJSON* mb = cJSON_GetObjectItem(root, "min_bid");
		v.minBid = (mb && cJSON_IsNumber(mb)) ? mb->valueint : 1;
		cJSON* mx = cJSON_GetObjectItem(root, "max_bid");
		v.maxBid = (mx && cJSON_IsNumber(mx)) ? mx->valueint : 0;
		cJSON* mbd = cJSON_GetObjectItem(root, "max_bidder");
		v.maxBidder = (mbd && cJSON_IsNumber(mbd)) ? mbd->valueint : -1;
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::BID) == 0) {
		cJSON* s = cJSON_GetObjectItem(root, "seat");
		cJSON* b = cJSON_GetObjectItem(root, "bid");
		int seat = (s && cJSON_IsNumber(s)) ? s->valueint : -1;
		int val = (b && cJSON_IsNumber(b)) ? b->valueint : 0;
		if (val > v.maxBid) { v.maxBid = val; v.maxBidder = seat; }
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::LANDLORD) == 0) {
		cJSON* s = cJSON_GetObjectItem(root, "seat");
		v.landlord = (s && cJSON_IsNumber(s)) ? s->valueint : -1;
		cJSON* bv = cJSON_GetObjectItem(root, "bid");
		v.bidValue = (bv && cJSON_IsNumber(bv)) ? bv->valueint : 1;
		cJSON* bt = cJSON_GetObjectItem(root, "bottom");
		v.bottomRevealed = true;
		if (bt && cJSON_IsArray(bt)) {
			int n = cJSON_GetArraySize(bt);
			if (n > DouDizhuLogic::BOTTOM_SIZE) n = DouDizhuLogic::BOTTOM_SIZE;
			for (int i = 0; i < n; i++) {
				cJSON* item = cJSON_GetArrayItem(bt, i);
				if (item && cJSON_IsNumber(item)) v.bottom[i] = (uint8_t)item->valueint;
			}
		}
		v.phase = 2;
		ESP_LOGI(TAG, "landlord=seat%d bid=%d", v.landlord, v.bidValue);
		// 地主拿底牌后各家具张数
		cJSON* cnt = cJSON_GetObjectItem(root, "counts");
		if (cnt && cJSON_IsArray(cnt)) {
			for (int i = 0; i < 3; i++) {
				cJSON* item = cJSON_GetArrayItem(cnt, i);
				if (item && cJSON_IsNumber(item)) v.cardCount[i] = item->valueint;
			}
		}
		// 若我是地主：本地把 3 张底牌并入自己手牌，保证手牌数与"剩余"一致
		// （主机随后也会单独发 HAND 覆盖，此处为双保险，避免手牌数量刷新异常）
		if (v.landlord == v.seat) {
			for (int k = 0; k < DouDizhuLogic::BOTTOM_SIZE; k++) v.hand[v.handCount++] = v.bottom[k];
			DouDizhuLogic::sortHand(v.hand, v.handCount);
		}
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::TURN) == 0) {
		cJSON* s = cJSON_GetObjectItem(root, "seat");
		v.turn = (s && cJSON_IsNumber(s)) ? s->valueint : -1;
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::PLAY) == 0) {
		cJSON* s = cJSON_GetObjectItem(root, "seat");
		int seat = (s && cJSON_IsNumber(s)) ? s->valueint : 0;
		if (seat < 0 || seat > 2) seat = 0;
		cJSON* pass = cJSON_GetObjectItem(root, "pass");
		bool isPass = pass && cJSON_IsTrue(pass);
		cJSON* nt = cJSON_GetObjectItem(root, "new_trick");
		bool newTrick = nt && cJSON_IsTrue(nt);
		if (newTrick) {
			for (int i = 0; i < 3; i++) { v.trickCount[i] = 0; v.trickPass[i] = false; }
		}
		if (isPass) {
			v.trickPass[seat] = true;
			v.trickCount[seat] = 0;
			// lastPlaySeat 保持原值：pass 不改变"当前墩最后出牌者"
		} else {
			cJSON* cards = cJSON_GetObjectItem(root, "cards");
			int n = 0;
			if (cards && cJSON_IsArray(cards)) {
				int sz = cJSON_GetArraySize(cards);
				if (sz > DouDizhuLogic::MAX_HAND) sz = DouDizhuLogic::MAX_HAND;
				for (int i = 0; i < sz; i++) {
					cJSON* item = cJSON_GetArrayItem(cards, i);
					if (item && cJSON_IsNumber(item)) v.trickCards[seat][n++] = (uint8_t)item->valueint;
				}
			}
			v.trickCount[seat] = n;
			v.trickPass[seat] = false;
			v.lastPlaySeat = seat;
		}
		cJSON* cnt = cJSON_GetObjectItem(root, "counts");
		if (cnt && cJSON_IsArray(cnt)) {
			for (int i = 0; i < 3; i++) {
				cJSON* item = cJSON_GetArrayItem(cnt, i);
				if (item && cJSON_IsNumber(item)) v.cardCount[i] = item->valueint;
			}
		}
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::HAND) == 0) {
		cJSON* cards = cJSON_GetObjectItem(root, "cards");
		int n = 0;
		if (cards && cJSON_IsArray(cards)) {
			int sz = cJSON_GetArraySize(cards);
			if (sz > DouDizhuLogic::MAX_HAND) sz = DouDizhuLogic::MAX_HAND;
			for (int i = 0; i < sz; i++) {
				cJSON* item = cJSON_GetArrayItem(cards, i);
				if (item && cJSON_IsNumber(item)) v.hand[n++] = (uint8_t)item->valueint;
			}
		}
		v.handCount = n;
		DouDizhuLogic::sortHand(v.hand, v.handCount);
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::GAME_OVER) == 0) {
		cJSON* lw = cJSON_GetObjectItem(root, "landlord_win");
		v.landlordWin = lw && cJSON_IsTrue(lw);
		cJSON* ml = cJSON_GetObjectItem(root, "multiplier");
		v.multiplier = (ml && cJSON_IsNumber(ml)) ? ml->valueint : 1;
		cJSON* sc = cJSON_GetObjectItem(root, "scores");
		if (sc && cJSON_IsArray(sc)) {
			for (int i = 0; i < 3; i++) { cJSON* item = cJSON_GetArrayItem(sc, i); if (item && cJSON_IsNumber(item)) v.scores[i] = item->valueint; }
		}
		cJSON* dl = cJSON_GetObjectItem(root, "delta");
		if (dl && cJSON_IsArray(dl)) {
			for (int i = 0; i < 3; i++) { cJSON* item = cJSON_GetArrayItem(dl, i); if (item && cJSON_IsNumber(item)) v.delta[i] = item->valueint; }
		}
		cJSON* sp = cJSON_GetObjectItem(root, "spring");
		v.spring = sp && cJSON_IsTrue(sp);
		cJSON* rs = cJSON_GetObjectItem(root, "reverse_spring");
		v.reverseSpring = rs && cJSON_IsTrue(rs);
		v.phase = 3;
		ESP_LOGI(TAG, "game over: landlordWin=%d mult=%d", v.landlordWin, v.multiplier);
		m_hasNew = true;
	}
	else if (strcmp(tag, DouDizhuMsg::ABORT) == 0) {
		v.phase = 0;
		m_hasNew = true;
	}

	cJSON_Delete(root);
}
