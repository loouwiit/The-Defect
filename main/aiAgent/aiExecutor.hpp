#pragma once

#include <string>
#include <vector>
#include <esp_log.h>
#include "aiProtocol.hpp"
#include "display/display.hpp"
#include "app/app.hpp"
#include "app/appStackManager.hpp"
#include "app/appFactory.hpp"
#include "audio/Audio.hpp"
#include "battery/batteryManager.hpp"
#include "wifi/wifi.hpp"

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
                action.type != AiActionType::ShowNotification &&
                action.type != AiActionType::ParseError &&
                activeApp)
            {
                handled = activeApp->executeAiAction(action);
            }

            // ── 第 2 步：App 未处理 → 系统兜底 ──
            if (!handled) {
                switch (action.type) {
                case AiActionType::RespondText:
                case AiActionType::ShowNotification:
                    handled = true;
                    break;

                case AiActionType::OpenApp:
                    handled = executeOpenApp(action, manager);
                    break;

                case AiActionType::NavigateBack:
                    if (manager) {
                        manager->pop();
                        handled = true;
                    }
                    break;

                case AiActionType::NavigateHome:
                    if (manager) {
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
                        int brightness = action.intValue;
                        if (brightness < 0) brightness = 0;
                        if (brightness > 100) brightness = 100;
                        display->setBrightness(static_cast<uint8_t>(brightness));
                        display->saveBrightness();
                        handled = true;
                    }
                    break;

                case AiActionType::SetVolume:
                    Audio::setMasterVolume(action.intValue);
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
                    auto ssidIt = action.params.find("ssid");
                    auto passIt = action.params.find("password");
                    if (ssidIt != action.params.end()) {
                        wifiConnect(ssidIt->second.c_str(),
                            passIt != action.params.end() ? passIt->second.c_str() : "");
                        handled = true;
                    }
                    break;
                }

                case AiActionType::SystemInfo:
                case AiActionType::PowerOff:
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
    /// 执行 open_app 操作
    static bool executeOpenApp(const AiAction& action, AppStackManager* manager)
    {
        if (!manager || action.appName.empty()) return false;

        // 通过 activeStack 的 top app 获取 Display
        auto* stack = manager->activeStack();
        if (!stack || stack->isEmpty()) return false;
        Display* display = stack->top()->getDisplay();

        App* app = AppFactory::create(action.appName, display);
        if (!app) {
            ESP_LOGE(TAG, "未知应用: %s", action.appName.c_str());
            return false;
        }

        struct param_t{ AppStackManager* stack; App* app; };
        Task::addTask([](void* param)->TickType_t
            {
                auto [manager, app] = *(param_t*)param;
                manager->pushToNewStack(app);
                delete (param_t*)param;
                return Task::infinityTime;
            }, "open app", new param_t{manager, app}, 100);
        return true;
    }

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
