bool serverIsStarted();
void serverStart(unsigned char maxAutoRestartTimes = 3);
void serverStop();

/** @brief 注册桌面导航回调（DesktopApp 调用，通过 VirtualIndev 注入触控） */
void desktopSetNavCb(void (*cb)(int action));
