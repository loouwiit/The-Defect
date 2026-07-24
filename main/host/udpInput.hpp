#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief 启动 UDP 游戏手柄输入监听（接收方使用）
 *
 * 在端口 8082 上监听 11 字节 UDP datagram，
 * 解析后注入 VirtualGamepad。
 *
 * 协议格式（与 /ws/input 11 字节帧一致）：
 *   [0]   hostId       — 设备号 (0=本机, 1-3=远程)
 *   [1]   localPlayer  — 设备上的手柄编号 (0-3)
 *   [2-3] buttons      — uint16 小端序
 *   [4]   lx
 *   [5]   ly
 *   [6]   rx
 *   [7]   ry
 *   [8]   lt
 *   [9]   rt
 *   [10]  dpad
 */
void udpInputStart();

/// 创建 UDP 发送 socket（持久复用，不要每次发送都创建）
/// 返回 socket fd，失败返回 -1
int udpInputCreateSocket(const char* host, uint16_t port);

/// 通过已有 socket 发送 11 字节帧（不关闭 socket）
/// sock 由 udpInputCreateSocket() 创建
bool udpInputSend(int sock, const uint8_t* data, size_t len);

/// 关闭 UDP 发送 socket
void udpInputCloseSocket(int sock);
