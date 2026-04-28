#include "uart.hpp"

UART::UART(GPIO RX, GPIO TX, GPIO RTS, GPIO CTS, UartPort port, int baudRate)
{
	this->port = port;
	uart_config_t uart_config{};
	uart_config.baud_rate = baudRate;
	uart_config.data_bits = UART_DATA_8_BITS;
	uart_config.parity = UART_PARITY_DISABLE;
	uart_config.stop_bits = UART_STOP_BITS_1;
	uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	uart_config.rx_flow_ctrl_thresh = 122;

    uart_set_pin(port, TX, RX, RTS, CTS);

	// Configure UART parameters
	ESP_ERROR_CHECK(uart_param_config(port, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(port, TX, RX, RTS, CTS));
}

UART::~UART()
{
	stop();
}

UART::UART(UART&& move)
{
    port = move.port;
    move.port = uart_port_t::UART_NUM_MAX;
}

UART& UART::operator=(UART&& move)
{
    if (this != &move)
    {
        stop();
        port = move.port;
        move.port = uart_port_t::UART_NUM_MAX;
    }
    return *this;
}

void UART::start()
{
	// Setup UART buffered IO with event queue
	constexpr static int uart_buffer_size = (1024 * 2);
	// QueueHandle_t uart_queue;
	// Install UART driver using an event queue here
	// ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0));
	ESP_ERROR_CHECK(uart_driver_install(port, uart_buffer_size, uart_buffer_size, 10, nullptr, 0));
}

void UART::stop()
{
	if (port == uart_port_t::UART_NUM_MAX)
		return;
	uart_driver_delete(port);
	port = uart_port_t::UART_NUM_MAX;
}

int UART::transmit(const void* data, size_t size)
{
	return uart_write_bytes(port, data, size);
}

void UART::waitForTransmit()
{
	// Wait for packet to be sent
	while (checkRecieve() == 0)
	{
		vTaskDelay(1);
	}
}

size_t UART::checkRecieve()
{
	size_t length = 0;
	ESP_ERROR_CHECK(uart_get_buffered_data_len(port, &length));
	return length;
}

int UART::recieve(char* data, size_t size)
{
	return uart_read_bytes(port, data, size, 100);
}

void UART::abandonRecieve()
{
	uart_flush(port);
}

