#pragma once

#include <string>
#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib>
#include "aiProtocol.hpp"

/**
 * @file aiParser.hpp
 * @brief AI 响应解析器
 *
 * 解析 OpenAI Chat Completion API 返回的 JSON，
 * 提取 actions[] 数组并转为 AiAction 列表。
 *
 * 鲁棒处理：
 * - 去除 markdown 代码块包裹 (```json ... ```)
 * - 自动查找最外层 {}
 * - 非法 JSON → ParseError action
 */

class AiParser {
public:
    constexpr static char TAG[] = "AiParser";

    /**
     * @brief 解析 OpenAI API 完整响应 JSON
     * @param apiResponse  esp_http_client 读取的原始响应文本
     * @return AiResponse  包含解析出的动作列表
     */
    static AiResponse parse(const std::string& apiResponse) {
        AiResponse result;
        result.rawContent = apiResponse;

        // 1. 解析顶层 JSON
        cJSON* root = cJSON_Parse(apiResponse.c_str());
        if (!root) {
            result.errorMsg = "顶层 JSON 解析失败";
            result.actions.push_back(makeError("顶层 JSON 解析失败"));
            return result;
        }

        // 2. 检查 API 错误
        cJSON* errObj = cJSON_GetObjectItem(root, "error");
        if (errObj) {
            cJSON* msg = cJSON_GetObjectItem(errObj, "message");
            if (msg && cJSON_IsString(msg))
                result.errorMsg = msg->valuestring;
            else
                result.errorMsg = "API 返回错误";
            cJSON_Delete(root);
            result.actions.push_back(makeError(result.errorMsg));
            return result;
        }

        // 3. 提取 choices[0].message.content
        cJSON* choices = cJSON_GetObjectItem(root, "choices");
        if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
            result.errorMsg = "响应中无 choices";
            cJSON_Delete(root);
            result.actions.push_back(makeError("AI 未返回有效响应"));
            return result;
        }

        cJSON* choice0 = cJSON_GetArrayItem(choices, 0);
        cJSON* message = cJSON_GetObjectItem(choice0, "message");
        cJSON* content = cJSON_GetObjectItem(message, "content");
        if (!cJSON_IsString(content) || !content->valuestring) {
            // 检查是否有 tool_calls（Function Calling 降级）
            cJSON* toolCalls = cJSON_GetObjectItem(message, "tool_calls");
            if (cJSON_IsArray(toolCalls) && cJSON_GetArraySize(toolCalls) > 0) {
                cJSON* call0 = cJSON_GetArrayItem(toolCalls, 0);
                cJSON* func = cJSON_GetObjectItem(call0, "function");
                cJSON* args = cJSON_GetObjectItem(func, "arguments");
                if (cJSON_IsString(args) && args->valuestring) {
                    result.rawContent = args->valuestring;
                    // 递归解析 arguments 中的 JSON
                    result = parseInner(args->valuestring);
                    cJSON_Delete(root);
                    return result;
                }
            }
            result.errorMsg = "响应中无 content";
            cJSON_Delete(root);
            result.actions.push_back(makeError("AI 响应内容为空"));
            return result;
        }

        std::string contentStr = content->valuestring;
        cJSON_Delete(root);

        // 4. 递归解析 content 中的 JSON
        result = parseInner(contentStr);
        result.rawContent = apiResponse;
        return result;
    }

private:
    /**
     * @brief 解析 AI 回复文本（应为 JSON actions 数组或对象）
     */
    static AiResponse parseInner(const std::string& text) {
        AiResponse result;

        // 1. 鲁棒处理：去除 markdown 代码块包裹
        std::string cleaned = stripMarkdown(text);

        // 2. 找到最外层 {}
        size_t start = cleaned.find('{');
        size_t end = cleaned.rfind('}');
        if (start == std::string::npos || end == std::string::npos || end <= start) {
            // 可能是裸数组 []
            size_t arrStart = cleaned.find('[');
            size_t arrEnd = cleaned.rfind(']');
            if (arrStart != std::string::npos && arrEnd != std::string::npos && arrEnd > arrStart) {
                // 包裹为 {"actions": [...]}
                cleaned = "{\"actions\":" + cleaned.substr(arrStart, arrEnd - arrStart + 1) + "}";
                start = 0;
                end = cleaned.size() - 1;
            } else {
                result.success = false;
                result.errorMsg = "未找到 JSON 对象";
                result.actions.push_back(makeError("AI 返回格式异常，请重试"));
                return result;
            }
        }

        std::string jsonStr = cleaned.substr(start, end - start + 1);

        // 3. cJSON 解析
        cJSON* root = cJSON_Parse(jsonStr.c_str());
        if (!root) {
            result.success = false;
            result.errorMsg = "JSON 解析失败";
            result.actions.push_back(makeError("AI 返回了无法解析的 JSON"));
            cJSON_Delete(root);
            return result;
        }

        // 4. 提取 actions 数组
        cJSON* actionsArr = cJSON_GetObjectItem(root, "actions");
        if (!actionsArr) {
            // 尝试将顶层视为单个 action 对象
            actionsArr = cJSON_CreateArray();
            cJSON_AddItemToArray(actionsArr, root);
            // 重新挂载
            cJSON* newRoot = cJSON_CreateObject();
            cJSON_AddItemToObject(newRoot, "actions", actionsArr);
            root = newRoot;
            actionsArr = cJSON_GetObjectItem(root, "actions");
        }

        if (!cJSON_IsArray(actionsArr)) {
            result.success = false;
            result.errorMsg = "actions 不是数组";
            result.actions.push_back(makeError("AI 返回格式异常"));
            cJSON_Delete(root);
            return result;
        }

        // 5. 遍历 actions 数组
        int size = cJSON_GetArraySize(actionsArr);
        for (int i = 0; i < size; i++) {
            cJSON* item = cJSON_GetArrayItem(actionsArr, i);
            AiAction action = parseAction(item);
            result.actions.push_back(action);
        }

        result.success = !result.actions.empty();
        cJSON_Delete(root);
        return result;
    }

    /// 解析单个 action 对象
    static AiAction parseAction(cJSON* item) {
        AiAction action;

        cJSON* act = cJSON_GetObjectItem(item, "action");
        if (!cJSON_IsString(act) || !act->valuestring) {
            action.type = AiActionType::UnknownAction;
            return action;
        }

        action.actionStr = act->valuestring;
        action.type = strToActionType(action.actionStr);

        // 读取通用字段
        auto readStr = [&](const char* key) -> std::string {
            cJSON* v = cJSON_GetObjectItem(item, key);
            if (cJSON_IsString(v) && v->valuestring)
                return v->valuestring;
            return "";
        };
        auto readInt = [&](const char* key, int def) -> int {
            cJSON* v = cJSON_GetObjectItem(item, key);
            if (cJSON_IsNumber(v))
                return v->valueint;
            return def;
        };

        action.text = readStr("text");
        if (action.text.empty()) action.text = readStr("app");  // fallback 旧格式
        action.value = readInt("value", 0);

        // 读取 params 对象 → cJSON 深拷贝
        cJSON* paramsObj = cJSON_GetObjectItem(item, "params");
        if (cJSON_IsObject(paramsObj)) {
            action.params = cJSON_Duplicate(paramsObj, true);
        }

        return action;
    }

    /// 字符串 → AiActionType
    static AiActionType strToActionType(const std::string& s) {
        if (s == "respond_text")      return AiActionType::RespondText;
        if (s == "open_app")          return AiActionType::OpenApp;
        if (s == "navigate_back")     return AiActionType::NavigateBack;
        if (s == "navigate_home")     return AiActionType::NavigateHome;
        if (s == "set_brightness")    return AiActionType::SetBrightness;
        if (s == "set_volume")        return AiActionType::SetVolume;
        if (s == "play_audio")        return AiActionType::PlayAudio;
        if (s == "wifi_connect")      return AiActionType::WifiConnect;
        if (s == "system_info")       return AiActionType::SystemInfo;
        if (s == "power_off")         return AiActionType::PowerOff;
        return AiActionType::UnknownAction;
    }

    /// 去除 markdown 代码块包裹
    static std::string stripMarkdown(const std::string& text) {
        std::string result = text;

        // 去除 ```json ... ```
        auto replace = [&](const std::string& prefix, const std::string& suffix) {
            size_t start = result.find(prefix);
            if (start != std::string::npos) {
                size_t end = result.find(suffix, start + prefix.size());
                if (end != std::string::npos) {
                    result = result.substr(start + prefix.size(), end - start - prefix.size());
                }
            }
        };
        replace("```json", "```");
        replace("```", "```");

        // 去除首尾空白
        auto trim = [](std::string& s) {
            size_t st = 0;
            while (st < s.size() && (s[st] == ' ' || s[st] == '\r' || s[st] == '\n' || s[st] == '\t'))
                st++;
            s.erase(0, st);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t'))
                s.pop_back();
        };
        trim(result);

        return result;
    }

    /// 创建解析错误 action
    static AiAction makeError(const std::string& msg) {
        AiAction a;
        a.type = AiActionType::ParseError;
        a.text = msg;
        return a;
    }
};
