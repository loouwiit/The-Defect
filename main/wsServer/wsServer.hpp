#pragma once

#include <cstdint>

/**
 * @brief 游戏按键事件回调
 * 由活跃的游戏 App 注册，wsServer 收到方向键后调用
 */
typedef void (*game_key_cb_t)(int player, uint8_t keyCode, bool pressed, void* ctx);

/**
 * @brief 注册当前活跃游戏的方向键回调
 * @param cb 回调函数，游戏 App 的 init() 中调用
 * @param ctx 上下文指针（如 SnakeGame*）
 */
void wsServerRegisterGameCallback(game_key_cb_t cb, void* ctx);

/**
 * @brief 注销回调（游戏 deinit 时调用）
 */
void wsServerUnregisterGameCallback();

bool wsServerStart();
void wsServerStop();
