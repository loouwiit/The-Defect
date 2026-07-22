#pragma once

#include <cstdint>
#include <functional>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class WsClient;

/**
 * @brief 主机管理器 — 跨主机连接的核心
 *
 * 单例，管理设备角色（主机/从机）、连接池、槽位数组。
 *
 * 角色：
 *   - 主机 (Host): 默认，可接受其他设备连接
 *   - 从机 (Client): 连接到远程主机后，放弃自身主机资格
 *
 * 槽位：
 *   4 个 {0=自身, 1-3=从机}，每个槽位关联一个 (hostId, hostname, ip)
 */
class HostManager {
public:
    static HostManager& instance();

    // ── 角色 ──
    bool isHost() const;       // 我是主机（默认，未连接他人）
    bool isClient() const;     // 我连接到了远程主机

    // ── 从机侧（主动连接远程主机）──
    bool connectToHost(const char* ip, uint16_t port = 8080);
    void disconnectFromHost();
    bool isConnectedToHost() const;
    const char* getRemoteHostname() const;

    // ── 获取 WebSocket 连接（从机调用）──
    // uri 示例: "/ws/tetris", "/ws/snake"
    // 返回指向主机该 URI 的 WsClient，同 URI 复用
    WsClient* getWsConnection(const char* uri);

    // ── 主机侧（被动接收从机连接）──
    struct ClientSlot {
        int slotId;            // 0=自身, 1-3=从机
        char hostname[64];     // 设备主机名
        char ip[16];           // IP 地址
        int gamepadCount;      // 该从机携带的手柄数
        bool connected;        // 是否已连接
    };

    int getConnectedClientCount() const;
    const ClientSlot* getSlots() const;  // 返回 4 个槽位的数组

    // ── 槽位操作 ──
    // 交换两个槽位的 hostId（主机调用，参考 BleSettingsApp 配对交换）
    // 广播 slot_swap 给所有从机
    bool swapSlots(int slotA, int slotB);

    // ── 从机分配的槽位编号（仅在 isClient() 时有效）──
    // slot 0 属于主机，此值为主机分配给本设备的 slot (1-3)
    int8_t myAssignedSlot() const { return m_myAssignedSlot; }
    // ── 通过控制通道发送消息（从机用此发送输入等数据）──
    bool sendOnConsole(const char* data, size_t len);

    // ── 获取输入转发队列（BLE Task 放，consolePoll Task 取+发）──
    QueueHandle_t getInputQueue();
    // ── 事件（供 ConsoleManager 更新 UI）──
    void setOnSlotUpdate(std::function<void(int slotId)> cb);

    // ── 广播函数（由 registerHostWsHandlers 设置为 wsServerBroadcastText）──
    void setBroadcastFn(std::function<void(const char*, size_t)> fn);
    void broadcast(const char* data, size_t len) const { if (m_broadcastFn) m_broadcastFn(data, len); }

    // ── 内部调用（由 wsConsoleHandler 调用）──
    // 从机注册/注销
    int registerClient(int fd, const char* hostname, int gamepadCount);
    void unregisterClient(int fd);
    // 通过 fd 查找 hostId
    int getHostIdByFd(int fd) const;

private:
    HostManager();
    ~HostManager() = default;
    HostManager(const HostManager&) = delete;
    HostManager& operator=(const HostManager&) = delete;

    static constexpr int MAX_CLIENTS = 4;
    static constexpr int MAX_WS_URIS = 8;

    // 角色
    bool m_isClient = false;
    int8_t m_myAssignedSlot = -1;  // 从机分配的槽位
    char m_remoteIp[16]{};
    uint16_t m_remotePort = 8080;
    char m_remoteHostname[64]{};

    // 槽位
    ClientSlot m_slots[MAX_CLIENTS]{};

    // 从机 fd → hostId 映射（主机侧）
    struct FdMap {
        int fd = -1;
        int hostId = -1;  // 1-3
    };
    FdMap m_fdMap[MAX_CLIENTS]{};

    // WsClient 连接池（从机侧，key=uri）
    struct WsPoolEntry {
        char uri[64]{};
        WsClient* client{};
    };
    WsPoolEntry m_wsPool[MAX_WS_URIS]{};

    // 管理通道 WsClient（从机侧，/ws/console 连接）
    WsClient* m_consoleClient{};

    // 事件
    std::function<void(int slotId)> m_onSlotUpdate;
    std::function<void(const char*, size_t)> m_broadcastFn;

    // 本机 hostname 缓存
    char m_selfHostname[64]{};

    // 控制通道轮询 Task + 输入转发队列
    void* m_consoleUpdateTask{};
    QueueHandle_t m_inputFwdQueue{};

    // 内部方法
    int findEmptySlot() const;
    int findSlotByFd(int fd) const;
    void parseSlotSync(const char* json);
};
