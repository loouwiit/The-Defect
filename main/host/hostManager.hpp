#pragma once

#include <cstdint>
#include <functional>
#include <cstring>

class WsClient;

/**
 * @brief 跨主机连接管理器 — 单例
 *
 * 职责：
 *   - 角色管理：isHost() / isClient()
 *   - 从机侧：connectToHost() → 建立 /ws/console 管理通道
 *   - 主机侧：维护 4 槽位 ClientSlot，供 ConsoleManager 展示
 *   - 连接池：getWsConnection(uri) 缓存 WsClient，同 URI 复用
 *
 * 使用方式：
 *   // 从机连接
 *   HostManager::instance().connectToHost("192.168.1.42");
 *   HostManager::instance().getWsConnection("/ws/tetris");  // → WsClient*
 *
 *   // 主机查询
 *   auto* slots = HostManager::instance().getSlots();
 *   slots[1].hostname  // "esp32p4-b"
 */
class HostManager {
public:
	static HostManager& instance();

	// ── 角色 ──
	bool isHost() const   { return !m_isClient; }
	bool isClient() const { return m_isClient; }

	// ── 从机侧（主动连接远程主机）──

	/// 连接到远程主机。连接成功后自动注册到 /ws/console。
	bool connectToHost(const char* ip, uint16_t port = 8080,
	                   const char* hostname = nullptr);

	/// 断开连接，恢复主机身份
	void disconnectFromHost();

	bool isConnectedToHost() const { return m_consoleClient != nullptr; }
	int  getMySlot() const         { return m_mySlot; }
	const char* getRemoteHostname() const { return m_remoteHostname; }
	const char* getRemoteIp() const       { return m_remoteIp; }
	uint16_t    getRemotePort() const     { return m_remotePort; }

	/// 获取 WebSocket 连接（从机调用）
	/// uri 示例: "/ws/tetris", "/ws/snake"
	/// 返回指向远程主机该 URI 的 WsClient，同 URI 复用
	WsClient* getWsConnection(const char* uri);

	// ── 主机侧（4 槽位状态）──

	struct ClientSlot {
		int  slotId;                // 0=本机, 1-3=从机
		char hostname[64]{};        // 设备主机名
		char ip[16]{};              // IP 地址
		int  gamepadCount = 0;      // 该设备上可用的手柄数
		bool connected = false;
	};

	/// 返回 4 槽位数组（索引即 slotId）
	const ClientSlot* getSlots() const { return m_slots; }

	// ── 事件回调（供 ConsoleManager 等 UI 监听）──

	void setOnSlotUpdate(std::function<void(const ClientSlot* slots)> cb) {
		m_onSlotUpdate = std::move(cb);
	}

	void setOnRoleChange(std::function<void(bool isClient)> cb) {
		m_onRoleChange = std::move(cb);
	}

	// ── 主机 mask（由 ConsoleManager 设置，wsConsoleHandler 广播）──

	void setHostMask(const uint8_t mask[4]);
	const uint8_t* getHostMask() const { return m_hostMask; }

	// ── 内部：由 wsConsoleHandler 调用 ──

	/// 注册/更新某槽位（主机收到 register 后调用）
	void onClientRegistered(int slotId, const char* hostname,
	                        const char* ip, int gamepadCount);

	/// 断开某槽位（主机检测到断开后调用）
	void onClientDisconnected(int slotId);

	/// 主机强制断开某个从机
	void disconnectClient(int slotId);

	/// 交换两个槽位（主机收到 swap 请求后调用）
	void swapSlots(int slotA, int slotB);

private:
	HostManager() = default;
	~HostManager() = default;
	HostManager(const HostManager&) = delete;

	// ── 角色 ──
	bool m_isClient = false;
	int  m_mySlot = -1;               // 从机时分配的 slot
	char m_remoteHostname[64] = {};
	char m_remoteIp[16] = {};
	uint16_t m_remotePort = 8080;

	// ── 连接池 ──
	struct WsEntry {
		char uri[64]{};
		WsClient* client = nullptr;
	};
	static constexpr int POOL_SIZE = 4;
	WsEntry m_wsPool[POOL_SIZE]{};
	WsClient* m_consoleClient = nullptr;  // /ws/console 连接

	WsClient* createOrGetWsClient(const char* uri);

	// ── 槽位状态（主机侧）──
	ClientSlot m_slots[4] = {
		{0, "本机", "", 0, true},
		{1, "",    "", 0, false},
		{2, "",    "", 0, false},
		{3, "",    "", 0, false},
	};

	// ── 主机 mask ──
	uint8_t m_hostMask[4] = {0x0F, 0x00, 0x00, 0x00};

	// ── 事件 ──
	std::function<void(const ClientSlot* slots)> m_onSlotUpdate;
	std::function<void(bool isClient)> m_onRoleChange;

	void notifySlotUpdate();
};
