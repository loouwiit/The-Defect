# 游戏市场系统

## 概述

The-Defect 内置游戏市场，可以从社区服务器浏览、下载、更新游戏。支持两种游戏类型：

| 类型 | 说明 | 存储位置 |
|------|------|----------|
| Lua 脚本 | 解释执行，沙箱化 | `/root/lua/market/<作者>/<游戏>/` |
| ELF 原生插件 | 编译为 .so，dlopen 加载 | `/root/elf/<id>/` |

## 市场客户端架构

```
MarketApp (UI)
    ↓
MarketClient (HTTP 异步)
    ↓
市场服务器 (The-Defect-Market)
```

### MarketClient API

| 方法 | 说明 |
|------|------|
| `fetchList(baseUrl, callback)` | 获取游戏列表 |
| `downloadScriptText(baseUrl, relPath, callback)` | 下载 Lua 脚本文本 |
| `downloadFile(baseUrl, relPath, destPath, callback)` | 下载文件（.so/图片等） |
| `reportDownload(baseUrl, id)` | 上报下载统计 |

### 服务器协议

| 接口 | 说明 |
|------|------|
| `GET /api/scripts?type=lua\|elf` | 游戏列表 |
| `GET /api/search?q=&type=` | 搜索 |
| `GET /games/<作者>/<游戏>/<file>` | 静态文件 |
| `POST /api/stats/download` | 下载统计 |

## 游戏类型判定

服务器扫描时根据目录内容自动判定：
- 目录含 `manifest.json` → `type="elf"`
- 否则 → `type="lua"`

ELF 条目的 `main`（.so 文件名）/ `manifest`（URL）/ `icon`（URL）由服务端补全。

## 市场 UI (MarketApp)

### 一级界面

- **3 列截图卡片网格**，Lua/ELF 混合显示
- 每卡左上角**类型徽标**（Lua=蓝 / ELF=紫）
- 右上角**已装角标**（下载/卸载即时更新）
- 缩略图懒加载，未加载显示评分占位

### 详情页

- 大图（自适应缩放）
- 类型/作者/版本/更新时间/下载量/评分
- 简介

### 操作

| 操作 | 说明 |
|------|------|
| 下载 | Lua: 保存到 `/root/lua/market/`；ELF: 安装到 `/root/elf/` |
| 更新 | 重新下载覆盖（不做版本比较） |
| 卸载 | Lua: 递归删除目录；ELF: `ElfManager::unload` + 递归删除 |

### 服务器编辑

底部栏"服务器"按钮可编辑市场服务器地址。确认后自动刷新列表。

## 市场服务器 (The-Defect-Market)

市场服务器是一个独立的 Git 子模块：

```bash
The-Defect-Market/
├── server/          ← 服务器代码
├── games/           ← 游戏内容
│   ├── <作者>/
│   │   ├── <游戏>/
│   │   │   ├── info.json    ← 元数据
│   │   │   ├── script.lua   ← Lua 脚本
│   │   │   ├── screenshot.png
│   │   │   └── thumb.png
│   │   └── ...
│   └── ...
└── ...
```

### info.json 格式

```json
{
  "name": "贪吃蛇",
  "author": "开发者",
  "version": "1.0.0",
  "description": "经典贪吃蛇游戏",
  "tags": ["经典", "休闲"],
  "rating": 4.5,
  "type": "lua"
}
```

### ELF 游戏额外字段

```json
{
  "type": "elf",
  "main": "snake.so",
  "manifest": "manifest.json",
  "icon": "icon.jpg"
}
```

## 已装识别

| 类型 | 检测方式 |
|------|----------|
| Lua | `testFile(本地脚本路径)` |
| ELF | `testFile(/root/elf/<id>/manifest.json)` |

## 典型工作流

### 发布 Lua 游戏

1. 在 `The-Defect-Market/games/<作者>/<游戏>/` 创建目录
2. 放入 `info.json` + `script.lua` + 截图
3. 推送到服务器仓库

### 发布 ELF 游戏

1. 用 `tools/build_elf.sh <name>` 构建
2. 将 `build/pkg/` 下的文件放入 `The-Defect-Market/games/<作者>/<游戏>/`
3. 创建 `info.json`（含 `type: "elf"`）
4. 推送到服务器仓库
