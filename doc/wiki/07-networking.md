# 网络与多主机联机

## 网络架构

```
┌─────────────────────────────────────────┐
│              ESP32-P4 (主控)              │
│                                          │
│  ┌──────────┐  ┌──────────┐             │
│  │ HTTP 80  │  │ WS 8080  │             │
│  │ 文件服务  │  │ WebSocket│             │
│  └──────────┘  └──────────┘             │
│                                          │
│  ┌──────────┐  ┌──────────┐             │
│  │ UDP 8082 │  │ mDNS     │             │
│  │ P4→P4    │  │ 设备发现  │             │
│  └──────────┘  └──────────┘             │
└──────────────┬──────────────────────────┘
               │ SDIO
┌──────────────┴──────────────────────────┐
│              ESP32-C6 (协处理器)          │
│                                          │
│  ┌──────────────────────┐               │
│  │ WiFi STA/AP + NAT    │               │
│  │ BLE NimBLE           │               │
│  └──────────────────────┘               │
└─────────────────────────────────────────┘
```

## WiFi 模式

### STA 模式（默认）

设备启动时尝试连接保存的 WiFi 网络。连接成功后：
- 获取 IP 地址
- mDNS 服务注册（`<hostname>.local`）
- SNTP 时间同步
- 游戏市场访问

### AP 模式（降级）

如果 STA 连接失败，自动降级为 AP 模式：
- SSID：`esp32p4`
- 无密码
- NAT 转发：AP 客户端可访问 STA 网络

### 切换

通过 WiFi 设置页面（`WifiSettingsApp`）或 AI 助手切换网络。

## mDNS 设备发现

设备启动后注册以下 mDNS 服务：

| 服务类型 | 端口 | 说明 |
|----------|------|------|
| `_http._tcp` | 80 | HTTP 服务 |
| `_ws._tcp` | 8080 | WebSocket 服务 |
| `_p4console._tcp` | 80 | 多主机发现专用 |

### 主机名

- 默认：`esp32p4`
- 可自定义：WiFi 设置页面底部"主机名"行
- NVS 持久化，修改后立即生效
- 访问：`http://<hostname>.local`

## 多主机联机

### 角色模型

| 角色 | 说明 |
|------|------|
| 主机 (Host) | 默认角色，管理游戏状态 |
| 从机 (Client) | 连接到主机，放弃自身主机资格 |

### 槽位管理

```
┌───┬───┬───┬───┐
│ 0 │ 1 │ 2 │ 3 │
│本机│ B │ --│ --│
└───┴───┴───┴───┘
```

- Slot 0：本机（始终存在）
- Slot 1-3：远程设备（通过 ConsoleManager 管理）

### 连接流程

```
从机 → 主机:  WS connect /ws/console
从机 → 主机:  {"type":"register","hostname":"...","gamepad_count":2}
主机 → 从机:  {"type":"registered","slot":1}
主机 → 所有:  slots_update（全量槽位 + mask 广播）
```

### 掩码控制

每个 (hostId, playerId) 对有独立的掩码位：
- bit = 1：该输入进入本地游戏
- bit = 0：该输入被忽略或转发到主机

通过 ConsoleManager UI 控制掩码分配。

## WebSocket 服务

端口 8080，支持以下 URI：

| URI | 说明 |
|-----|------|
| `/ws/stream` | 屏幕串流（MJPEG） |
| `/ws/tetris` | Tetris 游戏网络层 |
| `/ws/input` | Web 客户端手柄输入 |
| `/ws/console` | 多主机管理通道 |
| `/ws/log` | 实时日志推送 |
| `/ws/touch` | Web 客户端触摸注入 |

### /ws/input 协议

接收 11 字节二进制帧：

| 偏移 | 大小 | 字段 |
|------|------|------|
| 0 | 1 | hostId (0=本机, 1-3=远程) |
| 1 | 1 | localPlayer (0-3) |
| 2-3 | 2 | buttons (uint16 小端序) |
| 4 | 1 | lx (左摇杆 X) |
| 5 | 1 | ly (左摇杆 Y) |
| 6 | 1 | rx (右摇杆 X) |
| 7 | 1 | ry (右摇杆 Y) |
| 8 | 1 | lt (左扳机) |
| 9 | 1 | rt (右扳机) |
| 10 | 1 | dpad |

## UDP P4→P4 联机

端口 8082，用于两台 P4 设备间的低延迟输入转发。

- 协议：与 `/ws/input` 相同的 11 字节帧
- 传输：UDP，每个 datagram 原子到达
- Socket：持久 `connect()` + `send()`，零创建开销
- 丢包容忍：60fps 输入，丢一帧不影响后续

## HTTP 服务

端口 80，提供以下功能：

| 路径 | 说明 |
|------|------|
| `/` | 主页 |
| `/file` | 文件管理 |
| `/server/ota/` | OTA 升级页面 |
| `/server/screen/` | 屏幕串流查看器 |
| `/server/wifi/` | WiFi 配置 |
| `/server/diag/` | 系统诊断 |
| `/server/tetris/` | Tetris Web 客户端 |
| `/server/gamepad/` | 虚拟手柄 |
| `/root/...` | FAT 分区静态文件 |

### API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/log?tail=N` | 日志尾部 |
| POST | `/api/logLevel` | 调整日志级别 |
| POST | `/api/ota` | OTA 升级 |
| GET | `/api/otaInfo` | OTA 状态 |
| POST | `/api/ota/confirm` | 确认固件 |
| POST | `/api/ota/setBoot` | 切换启动分区 |
| GET | `/api/resetInfo` | 系统信息 |
| POST | `/api/resetInfo/clear` | 清除崩溃记录 |
| GET | `/api/coredump` | 下载崩溃转储 |
| POST | `/api/coredump/clear` | 清除崩溃转储 |
| GET/POST | `/api/wifiConfig` | WiFi 配置 |
| GET/POST | `/api/mdnsConfig` | mDNS 配置 |
| POST | `/api/aiConfig` | AI 配置 |
| POST | `/api/formatFlash` | 格式化 FAT |
| GET | `/api/getSpace` | 存储空间 |
