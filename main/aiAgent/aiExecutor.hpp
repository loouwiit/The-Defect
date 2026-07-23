#pragma once

#include <string>
#include <vector>
#include <esp_log.h>
#include "aiProtocol.hpp"
#include "aiAgent/aiSystemInfo.hpp"
#include "display/display.hpp"
#include "app/app.hpp"
#include "app/appStackManager.hpp"
#include "audio/Audio.hpp"
#include "battery/batteryManager.hpp"
#include "wifi/wifi.hpp"
#include "power/powerManager.hpp"
#include "gamepad/bleGamepad.hpp"

/**
 * @file aiExecutor.hpp
 * @brief AI 动作执行器
 *
 * 将 AiAction 列表按顺序执行：
 * 1. AppCustom → 路由到前台 App::executeAiAction()
 * 2. UnknownAction → 先尝试路由 App，失败则跳过
 * 3. 系统级动作 → 内置执行
 */

class AiExecutor {
public:
    constexpr static char TAG[] = "AiExecutor";

    struct Result {
        int totalCount = 0;
        int successCount = 0;
        std::string summary;  // 执行概述文本，用于 UI 显示
    };

    /**
     * @brief 顺序执行一组动作
     * @param actions  动作列表
     * @param display  显示驱动（用于获取前台 App）
     * @param manager  AppStackManager（用于导航操作）
     * @return 执行结果
     */
    static Result execute(const std::vector<AiAction>& actions,
        Display* display,
        AppStackManager* manager)
    {
        Result result;
        result.totalCount = actions.size();

        for (size_t i = 0; i < actions.size(); i++) {
            const auto& action = actions[i];
            bool handled = false;

            // 获取当前前台 App
            App* activeApp = display ? display->getActiveApp() : nullptr;

            // ── 第 1 步：App 优先拦截（RespondText/ParseError 除外） ──
            if (action.type != AiActionType::RespondText &&
                action.type != AiActionType::ParseError &&
                activeApp)
            {
                handled = activeApp->executeAiAction(action);
            }

            // ── 第 2 步：App 未处理 → 系统兜底 ──
            if (!handled) {
                switch (action.type) {
                case AiActionType::RespondText:
                    handled = true;
                    break;

                case AiActionType::Select:
                {
                    if (activeApp) {
                        AiSelectTarget target;
                        target.group = action.text;
                        target.index = action.value;
                        activeApp->onAiSelect(target);
                        handled = true;
                    }
                    break;
                }

                case AiActionType::Activate:
                {
                    if (activeApp) {
                        activeApp->onAiActivate();
                        handled = true;
                    }
                    break;
                }

                case AiActionType::NavigateBack:
                    if (manager) {
                        manager->pop();
                        handled = true;
                    }
                    break;

                case AiActionType::NavigateHome:
                    if (manager) {
                        // 1. 清空所有非根栈（pop 到空后自动销毁+切回根栈）
                        while (manager->stackCount() > 1) {
                            manager->switchToStack(manager->stackCount() - 1);
                            auto* stack = manager->activeStack();
                            while (stack && stack->depth() > 0) {
                                manager->pop();
                            }
                        }
                        // 2. 清空根栈到只剩桌面（depth=1）
                        manager->switchToStack(static_cast<size_t>(0));
                        auto* root = manager->activeStack();
                        while (root && root->depth() > 1) {
                            manager->pop();
                        }
                        handled = true;
                    }
                    break;

                case AiActionType::SetBrightness:
                    if (display) {
                        int brightness = action.value;
                        if (brightness < 0) brightness = 0;
                        if (brightness > 100) brightness = 100;
                        display->setBrightness(static_cast<uint8_t>(brightness));
                        display->saveBrightness();
                        handled = true;
                    }
                    break;

                case AiActionType::SetVolume:
                    Audio::setMasterVolume(action.value);
                    handled = true;
                    break;

                case AiActionType::PlayAudio:
                    if (!action.text.empty()) {
                        Audio::play(action.text.c_str()).detach();
                        handled = true;
                    }
                    break;

                case AiActionType::WifiConnect:
                {
                    const char* ssid = nullptr;
                    const char* pass = nullptr;
                    if (action.params) {
                        cJSON* ssidObj = cJSON_GetObjectItem(action.params, "ssid");
                        if (cJSON_IsString(ssidObj)) ssid = ssidObj->valuestring;
                        cJSON* passObj = cJSON_GetObjectItem(action.params, "password");
                        if (cJSON_IsString(passObj)) pass = passObj->valuestring;
                    }
                    if (ssid) {
                        wifiConnect(ssid, pass ? pass : "");
                        handled = true;
                    }
                    break;
                }

                case AiActionType::SystemInfo:
                {
                    std::string sysInfo = collectSystemInfo();
                    ESP_LOGI(TAG, "系统信息:\n%s", sysInfo.c_str());
                    handled = true;
                    break;
                }

                case AiActionType::PowerOff:
                {
                    ESP_LOGW(TAG, "AI 触发关机");
                    BleGamepad::instance().stopScan();
                    if (display) {
                        PowerManager::instance().prepareShutdown(
                            display->getLvglDisplay(), nullptr);
                    }
                    PowerManager::instance().shutdown();
                    handled = true;
                    break;
                }

                case AiActionType::ParseError:
                    handled = true;
                    break;

                default:
                    ESP_LOGW(TAG, "App 和系统均未处理: action=%s", action.text.c_str());
                    break;
                }
            }

            if (handled) {
                result.successCount++;
            }
        }

        // 生成摘要
        buildSummary(result);
        return result;
    }

private:
    /// 生成执行概述
    static void buildSummary(Result& result)
    {
        if (result.totalCount == 0) {
            result.summary = "无操作";
            return;
        }
        if (result.successCount == result.totalCount) {
            result.summary = "[v] " + std::to_string(result.totalCount) + " 个操作已执行";
        }
        else if (result.successCount > 0) {
            result.summary = "[!] " + std::to_string(result.successCount) + "/"
                + std::to_string(result.totalCount) + " 个操作已执行";
        }
        else {
            result.summary = "[x] 所有操作执行失败";
        }
    }
};
