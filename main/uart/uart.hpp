#include <driver/uart.h>
#pragma once

class UART
{
public:
	constexpr static int NoPin = UART_PIN_NO_CHANGE;

	UART(uart_port_t port = uart_port_t::UART_NUM_MAX);
	UART(UART&) = default;
	UART& operator=(UART&) = default;
	UART(UART&&) = default;
	UART& operator=(UART&&) = default;

	~UART();

	void setPin(int Tx = NoPin, int Rx = NoPin, int RTS = NoPin, int CTS = NoPin);
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
