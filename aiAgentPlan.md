# AI Agent 控制模块规划

## 设计理念

摒弃纯截图+点坐标的方案，改为 **「语义层」** 架构 —— App 主动向 Agent 描述自己能做什么，Agent 只需匹配用户意图到功能名，App 自身执行。

### 优势

| 对比 | 截图方案 | 语义方案 |
|------|---------|---------|
| 效率 | 每次截屏+编码+HTTP传图 | 仅传文本描述，数十 byte |
| 鲁棒性 | 依赖像素坐标，UI 变化即失效 | 功能 ID 不变，UI 随便改 |
| 深度 | 只能点坐标，无法表达意图 | App 可暴露任意功能 |
| 延迟 | ~3-5s/步 | ~0.5-1s/步 |

---

## 架构

```
用户意图 → AI Agent
            ↓
     ① activeApp->getAgentDescription()
        → JSON: 当前界面结构、可用功能
            ↓
     ② 描述 + 用户意图 → LLM
        → 返回: { function: "open_settings" }
            ↓
     ③ activeApp->executeAgentFunction("open_settings")
        → App 内部处理 → LVGL 更新
            ↓
     ④ (可选) 再次 getAgentDescription 确认状态 / 截屏兜底
```

### 混合策略：语义为主，视觉为辅

| 场景 | 方案 |
|------|------|
| 标准 App 界面（桌面/菜单/设置） | `getAgentDescription` + `executeAgentFunction` |
| 游戏画面 / 网页 / 未知界面 | 降级到截图 + 坐标点触（VirtualIndev） |
| LLM 返回 `{ "action": "coordinate" }` | 截图方案直接调 VirtualIndev |

LLM prompt 中告知优先走 function 路径，coordinates 作为 fallback。

---

## 模块结构

### 1. App 基类扩展 — `main/app/app.hpp`

新增两个虚方法，默认返回空（向后兼容）：

```cpp
virtual const char* getAgentDescription()   { return "{}"; }
virtual bool        executeAgentFunction(const char* fn, const char* paramsJson) { return false; }
```

### 2. AppStackManager 路由 — `main/app/appStackManager.hpp`

```cpp
const char* getActiveAppDescription();
bool        executeOnActiveApp(const char* fn, const char* params);
```

### 3. AI Agent 模块 — `main/aiAgent/`

```
aiAgent/
├── aiAgent.hpp       — 单例，编排 Agent 循环
├── aiAgent.cpp
├── llmClient.hpp     — HTTP 客户端，封装 LLM API 调用
├── llmClient.cpp
└── prompts.hpp       — 系统提示词模板
```

**Agent 循环流程**：

```
onUserMessage("打开音乐播放器")
  → getActiveAppDescription()  // 取当前界面描述
  → POST /v1/chat/completions  // 描述 + 用户指令
  → 解析返回: { function: "open:music", params: {} }
  → executeOnActiveApp("open:music", "{}")
  → 报告: "已打开音乐播放器"
```

### 4. Web 聊天 UI

基于现有 WebSocket 屏幕串流，添加聊天面板：

```
┌──────────────────────────┐
│  Agent 控制               │ ← 标题栏
├──────────────────────────┤
│                          │
│   (屏幕实时画面)          │ ← 已有 ws/stream
│                          │
├──────────────────────────┤
│  用户: 打开音乐播放器     │
│  Agent: 正在打开音乐播放  │ ← 对话历史
│  器... ✓                 │
├──────────────────────────┤
│ [输入框.........] [发送] │ ← 聊天输入
└──────────────────────────┘
```

页面内嵌到 HTTP server 的资源中。

### 5. HTTP API 路由

在 `serverKernal.cpp` 的 `httpPost()` 中添加：

| 路由 | 方法 | 功能 |
|------|------|------|
| `/api/agent/send` | POST | body=`{ "text": "用户指令" }` → 执行 Agent 循环 |
| `/api/agent/status` | GET | 返回 `{ "status": "idle" \| "busy", "app": "..." }` |

---

## DesktopApp 示例

```cpp
const char* getAgentDescription() override {
    return R"({
        "app": "DesktopApp",
        "current": 2,
        "games": ["音乐播放器", "2048", "设置"],
        "actions": [
            {"id": "next",      "label": "下一个"},
            {"id": "prev",      "label": "上一个"},
            {"id": "start",     "label": "启动选中"},
            {"id": "open:2048", "label": "打开2048"}
        ]
    })";
}

bool executeAgentFunction(const char* fn, const char*) override {
    if (strcmp(fn, "next") == 0)  { next();  return true; }
    if (strcmp(fn, "prev") == 0)  { prev();  return true; }
    if (strcmp(fn, "start")== 0)  { start(); return true; }
    if (strncmp(fn, "open:", 5) == 0) {
        for (int i = 0; i < GAME_COUNT; i++)
            if (strcmp(fn + 5, GAME_NAMES[i]) == 0) {
                selectGame(i); start(); return true;
            }
    }
    return false;
}
```

---

## LLM API 接入

### 选型

- **首选：DeepSeek Chat API**（国内可直连，性价比高，纯文本流）
- 备选：OpenAI GPT-4o-mini, Claude 3 Haiku

### HTTP 客户端设计

- 使用 ESP-IDF `esp_http_client` 或裸 socket + JSON 组装
- PSRAM 分配请求/响应缓冲区（文本量小，~4KB 足矣）
- 异步 Task，不阻塞 LVGL / HTTP server

### 系统提示词

```text
你是 ESP32-P4 游戏机的 AI 助手。用户将通过文字描述操作意图。

当前界面描述由 App 提供（JSON 格式），包含可用操作列表。
请根据用户意图选择一个最匹配的操作并返回 JSON：
{
  "function": "action_id",
  "params": {},
  "reason": "简短说明",
  "done": false   // true=任务已完成，false=还需更多操作
}

如果界面描述中没有匹配的操作，可以返回：
{
  "function": "__ask__",
  "reason": "抱歉，当前界面不支持此操作"
}

也可以返回坐标点触作为降级方案：
{
  "function": "__coordinate__",
  "params": { "x": 300, "y": 500 },
  "reason": "使用坐标点击"
}
```

---

## 实现步骤

| # | 内容 | 文件 |
|---|------|------|
| 1 | App 基类新增 `getAgentDescription()` / `executeAgentFunction()` | `main/app/app.hpp` |
| 2 | AppStackManager 新增路由方法 | `main/app/appStackManager.hpp` / `.cpp` |
| 3 | DesktopApp 实现 agent 描述 + 功能映射 | `main/app/desktopApp/desktopApp.cpp` |
| 4 | 创建 `main/aiAgent/` 目录 + CMakeLists 注册 | `main/CMakeLists.txt` |
| 5 | 实现 `llmClient` — HTTP 调用 LLM API | `main/aiAgent/llmClient.hpp/.cpp` |
| 6 | 实现 `aiAgent` 编排循环 | `main/aiAgent/aiAgent.hpp/.cpp` |
| 7 | 系统提示词模板 | `main/aiAgent/prompts.hpp` |
| 8 | Web 聊天 HTML 页面 | 内嵌或 `resources/server/` |
| 9 | HTTP 路由 `/api/agent/send` + `/api/agent/status` | `main/server/serverKernal.cpp` |
| 10 | `main.cpp` 初始化 Agent | `main/main.cpp` |
| 11 | 端到端联调 | — |

---

## 风险与注意事项

| 风险 | 缓解 |
|------|------|
| API Key 安全 | 编译时内嵌或首次启动从 FAT 分区读取 |
| 网络不稳定 | 超时重试 + 本地缓存上次描述 |
| 多 App 逐步适配 | 默认空实现，零侵入；新 App 按需实现 |
| 并发安全 | Agent 使用独立 Task，通过队列与 AppStackManager 交互 |
| 对话历史 | 保留最近 N 轮 `{role, content}` 上下文 |
