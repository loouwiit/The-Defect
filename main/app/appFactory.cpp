#include "appFactory.hpp"
#include "app/tetris/tetrisApp.hpp"
#include "app/snake/snakeRoom/snakeRoom.hpp"
#include "app/fruitNinja/fruitNinjaRoom.hpp"
#include "app/chineseChess/chineseChessRoom.hpp"
#include <cstring>
#include <esp_log.h>

static constexpr char TAG[] = "AppFactory";

App* AppFactory::create(const std::string& name, Display* display) {
    if (name == "tetris") {
        return new TetrisApp(display);
    }
    if (name == "snake") {
        return new SnakeRoom(display);
    }
    if (name == "fruitninja" || name == "fruit_ninja" || name == "fruitNinja") {
        return new FruitNinjaRoom(display);
    }
    if (name == "chinesechess" || name == "chinese_chess" || name == "chineseChess") {
        return new ChineseChessRoom(display);
    }

    ESP_LOGE(TAG, "未识别的应用: %s", name.c_str());
    return nullptr;
}
