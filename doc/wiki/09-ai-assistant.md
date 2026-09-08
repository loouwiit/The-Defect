# AI 助手

## 概述

The-Defect 内置 AI 助手，基于 OpenAI 兼容 API，可以：
- 对话聊天
- 控制设备（打开应用、调亮度、连 WiFi 等）
- 生成并运行 Lua 游戏
- 多轮协作完成复杂任务

## 唤醒方式

| 方式 | 操作 |
|------|------|
| 手势 | 屏幕顶部下滑 |
| 桌面卡片 | 点击桌面 AI 卡片 |
| 手柄组合键 | Y + X + B |

## AI Agent 架构

```
用户输入
    ↓
AiClient (HTTPS POST → OpenAI API)
    ↓
AI 响应 (JSON)
    ↓
AiParser (解析 actions[] + memory + nextRound)
    ↓
AiExecutor (按顺序执行 14 个系统动作)
    ↓
nextRound? → 继续对话（最多 5 轮）
    ↓
memory → 长期记忆存储
```

## 系统动作

| 动作 | 说明 | 参数 |
|------|------|------|
| `respond_text` | 回复文字 | `text` |
| `open_app` | 打开应用 | `text`（应用名） |
| `select` | 移动焦点 | `text`（组名）, `value`（索引） |
| `activate` | 触发当前焦点 | — |
| `navigate_back` | 返回上一级 | — |
| `navigate_home` | 回桌面 | — |
| `set_brightness` | 调亮度 | `value`（0-100） |
| `set_volume` | 调音量 | `value`（0-100） |
| `play_audio` | 播放音频 | `text`（文件路径） |
| `wifi_connect` | 连接 WiFi | `params`（ssid/password） |
| `system_info` | 获取设备信息 | — |
| `power_off` | 关机 | — |
| `generate_lua_game` | 生成 Lua 游戏 | `text`（描述）, `params.code`（代码） |
| `read` | 读取文件内容 | `text`（路径）, `nextRound:true` |

## 多轮对话

AI 可以通过 `nextRound: true` 请求多轮协作：

```
用户: "帮我做一个贪吃蛇游戏"
  ↓
AI: generate_lua_game → 运行游戏
  ↓ (nextRound: true)
AI: 探索新界面，获取上下文
  ↓ (nextRound: true)
AI: 根据运行结果调整
  ↓ (最多 5 轮)
完成
```

## 长期记忆

AI 通过 `memory` 字段增量更新长期记忆，存储在设备上。下次对话时自动注入 system prompt。

## 输入法

AI 助手输入法支持三种模式：

| 模式 | 说明 |
|------|------|
| 拼音 | 中文拼音输入，词库 411 码/8343 字 |
| 五笔 | 五笔+拼音混合，词库 22088 码/25042 字 |
| English | 英文直接上屏 |

切换方式：键盘底部 🔣 按钮循环切换。

### 手柄操作

| 操作 | 按键 |
|------|------|
| 键盘导航 | ← → ↑ ↓（左摇杆） |
| 确认/选候选字 | A / L3 |
| 退格删除 | B |
| 发送消息 | Y |
| 键盘/候选面板切换 | X |
| 中英文切换 | 键盘底部 🔣 |
| 候选面板翻页 | ↑ ↓（焦点在候选面板时） |

## App AI 接口

每个 App 可以通过以下虚方法接入 AI 系统：

```cpp
class App {
    // 返回 App 描述（拼入 AI prompt）
    virtual std::string getAiContext();

    // 焦点系统
    virtual void onAiSelect(const AiSelectTarget& target);
    virtual void onAiActivate();

    // 自定义动作拦截
    virtual bool executeAiAction(const AiAction& action);

    // 可打开的应用列表
    virtual std::vector<AiAppTarget> getOpenableApps();
};
```

## 配置

AI 配置存储在 `/root/system/ai_config.json`：

```json
{
  "api_key": "sk-...",
  "base_url": "https://api.openai.com/v1",
  "model": "gpt-4o"
}
```

也可通过 HTTP API 配置：
```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"api_key":"sk-...","base_url":"https://api.openai.com/v1","model":"gpt-4o"}' \
  http://<主机名>.local/api/aiConfig
```