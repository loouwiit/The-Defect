# 编译、烧录与 OTA 升级

## 环境搭建

### 前置要求

- **ESP-IDF v5.5.2**：[安装指南](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.5.2/esp32p4/get-started/index.html)
- **Python 3.8+**
- **Ninja 构建系统**（idf.py 默认使用）
- **Git**（含 submodule 支持）

### 克隆仓库

```bash
git clone --recurse-submodules <仓库地址>
cd The-Defect
```

如果已经克隆但缺少子模块：
```bash
git submodule update --init --recursive
```

### 激活 ESP-IDF 环境

```bash
. $IDF_PATH/export.sh
```

## 编译

### 使用 ESP-IDF VS Code 扩展

推荐使用 `@espIdfCommands` 工具进行编译、烧录、监控等操作。

### 命令行编译

```bash
idf.py build
```

编译产物：
- `build/The-Defect.bin` — 应用镜像（OTA 升级用）
- `build/The-Defect.elf` — ELF 可执行文件（调试用）
- `build/bootloader/bootloader.bin` — 引导加载程序
- `build/partition_table/partition-table.bin` — 分区表

### 编译选项

项目使用 `sdkconfig.defaults` 预配置，关键选项：

| 选项 | 值 | 说明 |
|------|----|------|
| CPU 频率 | 360MHz | 双核 RISC-V |
| PSRAM | Octal 200MHz | 主堆 |
| Flash | 32MB QIO 80MHz | 大容量 |
| FreeRTOS | 1000Hz | 高精度定时 |
| 编译优化 | Debug (-Og) | 开发阶段 |

## 首次烧录（USB）

封壳前需要通过 USB 进行首次全量烧录：

```bash
idf.py -p /dev/ttyACMx flash
```

这会烧录：
- 引导加载程序（bootloader）
- 分区表
- OTA 数据
- 应用镜像

### 烧录分区说明

| 分区 | 偏移 | 说明 |
|------|------|------|
| bootloader | 0x2000 | 引导加载程序 |
| partition-table | 0x10000 | 分区表 |
| ota_data_initial | 0x15000 | OTA 初始数据 |
| app (ota_0) | 见分区表 | 应用镜像 |

## OTA 空中升级

> ⚠ **封壳设备无 USB 调试口，OTA 是唯一的升级途径。**

### 通过 Web 页面升级

1. 浏览器打开 `http://<主机名>.local/server/ota/`
2. 选择 `build/The-Defect.bin` 文件
3. 点击上传，等待进度条完成
4. 设备自动重启

### 通过 curl 命令行升级

```bash
curl -X POST \
  --data-binary @build/The-Defect.bin \
  http://<主机名>.local/api/ota
```

### OTA 安全机制

| 机制 | 说明 |
|------|------|
| 双槽交替 | ota_0 ↔ ota_1 交替写入 |
| 自动回滚 | 未确认的固件崩溃后 bootloader 自动回退旧版 |
| 版本防重烧 | 相同版本号拒绝重复写入 |
| 手动确认 | 验证正常后调用 `POST /api/ota/confirm` 标记固件有效 |
| 手动切换 | `POST /api/ota/setBoot` 可手动切换启动分区 |

### OTA 流程

```
上传 .bin → 流式写入目标分区
  → 镜像校验 → 设置启动分区 → 1s 后重启
  → bootloader 检查新固件 → 启动
  → 10s 内崩溃? → 自动回滚旧版
  → 运行正常? → 用户确认 → 标记 VALID
```

### 查看 OTA 状态

```bash
curl http://<主机名>.local/api/otaInfo
```

返回 JSON 包含：当前版本、目标版本、回滚记录、各槽状态等。

## OTA 回滚确认

OTA 升级后新固件处于 `PENDING_VERIFY` 状态，回滚窗口打开。验证正常后：

```bash
curl -X POST http://<主机名>.local/api/ota/confirm
```

> ⚠ 未确认时任何重启都会触发 bootloader 自动回退旧版。

## 分区管理

### 查看分区信息

```bash
curl http://<主机名>.local/api/otaInfo
```

### 手动切换启动分区

```bash
# 切换到 ota_1 启动
curl -X POST -H "Content-Type: application/json" \
  -d '{"slot":"ota_1","invalidate":false}' \
  http://<主机名>.local/api/ota/setBoot

# 手动回退（当前分区标 INVALID）
curl -X POST -H "Content-Type: application/json" \
  -d '{"slot":"ota_1","invalidate":true}' \
  http://<主机名>.local/api/ota/setBoot
```

## 构建 ELF 插件

```bash
# 构建单个插件
tools/build_elf.sh snake

# 产物在 build/pkg/ 下：
# - snake.so
# - manifest.json
# - icon.jpg

# 上传到设备
curl -T build/pkg/snake.so http://<主机名>.local/elf/snake/snake.so
curl -T build/pkg/manifest.json http://<主机名>.local/elf/snake/manifest.json
curl -T build/pkg/icon.jpg http://<主机名>.local/elf/snake/icon.jpg
```

## 远程调试

### 查看日志

```bash
# 获取日志尾部
curl http://<主机名>.local/api/log?tail=100

# 浏览器实时日志
# 打开 http://<主机名>.local/server/diag/ → 实时日志标签
```

### 调整日志级别

```bash
# 全局设为 DEBUG
curl -X POST -H "Content-Type: application/json" \
  -d '{"level":4}' \
  http://<主机名>.local/api/logLevel

# 按 tag 过滤
curl -X POST -H "Content-Type: application/json" \
  -d '{"tag":"BleGamepad","level":1}' \
  http://<主机名>.local/api/logLevel
```

### 下载崩溃转储

```bash
curl -o core.elf http://<主机名>.local/api/coredump
idf.py coredump-info -c core.elf build/The-Defect.elf
```

### 查看系统信息

```bash
curl http://<主机名>.local/api/resetInfo
```
