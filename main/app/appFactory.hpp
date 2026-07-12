#pragma once

#include <string>
#include "app/app.hpp"

/**
 * @file appFactory.hpp
 * @brief 应用工厂
 *
 * 根据名称创建 App 实例，集中管理 name→App 映射。
 * 供 DesktopApp 的 executeAiAction 和 AiExecutor 共用。
 */

class AppFactory {
public:
    /**
     * @brief 根据名称创建 App 实例
     * @param name  应用名（小写英文，与 AI prompt 一致）
     * @param display  显示驱动
     * @return App* 或 nullptr（未找到）
     */
    static App* create(const std::string& name, Display* display);
};
