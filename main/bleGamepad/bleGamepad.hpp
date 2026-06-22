#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include "gamepadState.hpp"
#include "mutex/mutex.hpp"

// 前置声明 esp_event_base_t (esp_event_base.h 定义)
typedef const char* esp_event_base_t;

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// 前置声明 — NimBLE GAP 事件结构体和自由函数回调
struct ble_gap_event;
int bleGapEventCb(struct ble_gap_event* event, void* arg);
void mySyncCb();
void myResetCb(int reason);

class BleGamepad
{
public:
    static BleGamepad& instance();

    bool start();
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
    bool initNimbleAndHidHost();

    // 查找空闲 playerId
    int allocPlayerId();

    // 直接连接设备（原始 NimBLE GAP）
    bool connectDirect(const ScanDevice& dev);

    // GAP 连接事件回调（静态，用作 C 回调）
    static int connectGapEvent(struct ble_gap_event* event, void* arg);

    // 通过 esp_hidh_dev_t* 查找 playerId
    int playerIdByDev(void* dev) const;

    // 内部状态
    DeviceContext m_devices[MAX_PLAYERS]{};
    std::vector<ScanDevice> m_scanResults;
    mutable Mutex m_mutex;

    QueueHandle_t m_inputQueue{};
    TaskHandle_t m_processTask{};

    uint16_t m_connHandle{};

    bool m_scanning{false};
    bool m_running{false};

    static BleGamepad* s_instance;

    // NimBLE 任务入口
    static void hostTask(void* arg);

    // 输入处理任务入口
    static void processTask(void* arg);

    // HID Host 事件回调
    static void hidhCallback(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);

    // 自由函数回调——声明为友元以访问私有成员
    friend int  ::bleGapEventCb(struct ble_gap_event* event, void* arg);
    friend void ::mySyncCb();
    friend void ::myResetCb(int reason);
};
