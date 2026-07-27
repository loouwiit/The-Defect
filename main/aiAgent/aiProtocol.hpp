#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cJSON.h>

/**
 * @file aiProtocol.hpp
 * @brief AI Agent 协议定义
 *
 * 定义 AI 与设备之间的通信协议：
 * - AiActionType: 所有可执行的动作类型
 * - AiAction: 统一的动作数据结构
 * - AiResponse: OpenAI API 完整响应的解析结果
 */

// ============================================================
//  AIActionType — 动作类型枚举
// ============================================================

enum class AiActionType : uint8_t {
    // ── 系统级操作 ──
    RespondText,        // 显示文字回复. params: text
    OpenApp,            // 打开应用. params: app (tetris/snake/...)
    NavigateBack,       // 返回上一级
    NavigateHome,       // 回桌面
    SetBrightness,      // 调亮度 0-100. params: value
    SetVolume,          // 调音量 0-100. params: value
    PlayAudio,          // 播放音频文件. params: path
    WifiConnect,        // 连接 WiFi. params: ssid, password
    SystemInfo,         // 显示设备信息
    Select,             // 聚焦到指定元素（text=组名, value=索引）
    Activate,           // 触发当前聚焦元素
    PowerOff,           // 关机（Deep-sleep）
    GenerateLuaGame,    // AI 生成 Lua 游戏代码并运行. params: code, title
    ReadFile,           // 读取文件内容并返回给 AI（下一轮可见）. text: 文件路径

    // ── 内部用 ──
    ParseError,         // JSON 解析失败
    UnknownAction,      // 未识别的 action 字符串 → 透传给前台 App
};

// ============================================================
//  AiAction — 统一的动作数据结构
// ============================================================

struct AiAction {
    AiActionType type{ AiActionType::UnknownAction };

    // 原始 action 字符串（AI 回复中的 "action" 字段原文）
    std::string actionStr;

    // 通用字符串参数（app名、回复文本、路径等）
    std::string text;

    // 数值参数（亮度、音量等）
    int value{};

    // 额外参数（cJSON 对象，所有权转移，自动管理生命周期）
    cJSON* params{};

    ~AiAction() { if (params) cJSON_Delete(params); }

    AiAction() = default;

    AiAction(const AiAction& o)
        : type(o.type), actionStr(o.actionStr), text(o.text),
          value(o.value), params(o.params ? cJSON_Duplicate(o.params, true) : nullptr) {}

    AiAction(AiAction&& o) noexcept
        : type(o.type), actionStr(std::move(o.actionStr)), text(std::move(o.text)),
          value(o.value), params(o.params) { o.params = nullptr; }

    AiAction& operator=(const AiAction& o) {
        if (this != &o) {
            if (params) cJSON_Delete(params);
            type = o.type; actionStr = o.actionStr; text = o.text;
            value = o.value;
            params = o.params ? cJSON_Duplicate(o.params, true) : nullptr;
        }
        return *this;
    }

    AiAction& operator=(AiAction&& o) noexcept {
        if (this != &o) {
            if (params) cJSON_Delete(params);
            type = o.type; actionStr = std::move(o.actionStr); text = std::move(o.text);
            value = o.value; params = o.params; o.params = nullptr;
        }
        return *this;
    }
};

// ============================================================
//  AiResponse — OpenAI API 响应解析结果
// ============================================================

struct AiResponse {
    bool success = false;                           // 整体是否成功
    std::vector<AiAction> actions;                  // 解析出的动作列表
    std::string errorMsg;                           // 错误描述（success=false 时）
    std::string rawContent;                         // AI 原始回复文本（调试用）
    std::string memory;                             // 长期记忆（AI 可选写入，增量更新）
    bool nextRound = false;                         // true=本轮执行后继续下一轮（App 切换等场景）
};
