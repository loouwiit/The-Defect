#pragma once

#include <driver/uart.h>
#include "gpio/gpio.hpp"

class UART
{
public:
    using UartPort = uart_port_t;
	constexpr static int NoPin = UART_PIN_NO_CHANGE;

    UART() = default;
	UART(GPIO RX, GPIO TX, GPIO RTS, GPIO CTS, UartPort port = UART_NUM_MAX, int baudRate = 115200);
    ~UART();

    UART(UART&) = delete;
	UART& operator=(UART&) = delete;

    UART(UART&& move);
	UART& operator=(UART&& move);

	void start();
	void stop();

	int transmit(const void* data, size_t size);
	void waitForTransmit();

	size_t checkRecieve();
	int recieve(char* data, size_t size);
	void abandonRecieve();

private:
	uart_port_t port;
};
