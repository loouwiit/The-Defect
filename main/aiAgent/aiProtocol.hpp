#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>

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
    ShowNotification,   // 显示通知. params: text, type(info/success/error)
    PowerOff,           // 关机（需确认）

    // ── App 自定义操作 ──
    AppCustom,          // → 路由到前台 App::executeAiAction()

    // ── 内部用 ──
    ParseError,         // JSON 解析失败
    UnknownAction,      // 未识别的 action 字符串
};

// ============================================================
//  AiAction — 统一的动作数据结构
// ============================================================

struct AiAction {
    AiActionType type{ AiActionType::UnknownAction };

    // 通用字段
    std::string text;                               // respond_text / notification 内容

    // 应用操作
    std::string appName;                            // open_app 目标应用名

    // 数值参数
    int intValue{};                                 // set_brightness / set_volume 值

    // app_custom 用
    std::string command;                            // 自定义命令名
    std::map<std::string, std::string> params;      // 扩展参数字典
};

// ============================================================
//  AiResponse — OpenAI API 响应解析结果
// ============================================================

struct AiResponse {
    bool success = false;                           // 整体是否成功
    std::vector<AiAction> actions;                  // 解析出的动作列表
    std::string errorMsg;                           // 错误描述（success=false 时）
    std::string rawContent;                         // AI 原始回复文本（调试用）
};
