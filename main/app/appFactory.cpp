#include "appFactory.hpp"
#include "app/tetris/tetrisRoomApp.hpp"
#include "app/snake/snakeRoom/snakeRoom.hpp"
#include "app/fruitNinja/fruitNinjaRoom.hpp"
#include "app/chineseChess/chineseChessRoom.hpp"
#include "app/bleSettingsApp/bleSettingsApp.hpp"
#include "app/wifiSettingsApp/wifiSettingsApp.hpp"
#include "app/timeSettingsApp/timeSettingsApp.hpp"
#include "app/powerManagementApp/powerManagementApp.hpp"
#include <cstring>
#include <esp_log.h>

static constexpr char TAG[] = "AppFactory";

App* AppFactory::create(const std::string& name, Display* display) {
    if (name == "tetris") {
        return new TetrisRoomApp(display);
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
    if (name == "timesettings" || name == "time_settings" || name == "timeSettings") {
        return new TimeSettingsApp(display);
    }
    if (name == "wifisettings" || name == "wifi_settings" || name == "wifiSettings") {
        return new WifiSettingsApp(display);
    }
    if (name == "blesettings" || name == "ble_settings" || name == "bleSettings") {
        return new BleSettingsApp(display);
    }
    if (name == "powermanagement" || name == "power_management" || name == "powerManagement") {
        return new PowerManagementApp(display);
    }

    ESP_LOGE(TAG, "未识别的应用: %s", name.c_str());
    return nullptr;
}
