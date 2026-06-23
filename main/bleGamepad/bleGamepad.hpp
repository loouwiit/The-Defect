#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "gamepadState.hpp"
#include "mutex/mutex.hpp"
#include "display/display.hpp"

// 前置声明 esp_event_base_t (esp_event_base.h 定义)
typedef const char* esp_event_base_t;

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// 前置声明 NimBLE 类型
struct ble_gap_event;

class BleGamepad
{
public:
    static BleGamepad& instance();

    bool start(Display* display);
    void stop();

    void startScan();
    void stopScan();

    void connect(uint8_t scanIndex);
    void disconnect(uint8_t playerId);
    void disconnectAll();

    uint8_t connectedCount() const;
    const DeviceContext* getDevice(uint8_t playerId) const;

    std::vector<ScanDevice> getScannedDevices() const;

private:
    BleGamepad() = default;
    ~BleGamepad() = default;

    BleGamepad(const BleGamepad&) = delete;
    BleGamepad& operator=(const BleGamepad&) = delete;

    // 内部初始化步骤
    bool initEspHostedBt();
    bool initNimble();

    // 查找空闲 playerId
    int allocPlayerId();

    // 通过 conn_handle 查找 playerId
    int playerIdByConnHandle(uint16_t handle) const;

    // 直接连接设备（原始 NimBLE GAP）
    bool connectDirect(const ScanDevice& dev);

    // GAP 连接事件回调（静态，用作 C 回调）
    static int connectGapEvent(struct ble_gap_event* event, void* arg);

    // 内部状态
    Display* m_display{};
    DeviceContext m_devices[MaxPlayers]{};
    std::vector<ScanDevice> m_scanResults;
    mutable Mutex m_mutex;

    QueueHandle_t m_inputQueue{};
    TaskHandle_t m_processTask{};

    bool m_scanning{false};
    bool m_running{false};

    static BleGamepad* s_instance;

    // NimBLE 任务入口
    static void hostTask(void* arg);

    // 输入处理任务入口
    static void processTask(void* arg);

    // NimBLE 扫描 GAP 事件回调
    static int scanGapEventCb(struct ble_gap_event* event, void* arg);
    // NimBLE 同步/重置回调
    static void onSync();
    static void onReset(int reason);
};
