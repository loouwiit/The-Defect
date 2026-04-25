#pragma once

#include <driver/gpio.h>

class GPIO
{
public:
	using GPIO_NUM = gpio_num_t;
	using Mode = gpio_mode_t;
	using Pull = gpio_pull_mode_t;
	using Interrupt = gpio_int_type_t;
	using function_t = void(*)(void*);

	constexpr static GPIO_NUM NC = GPIO_NUM::GPIO_NUM_NC;

	GPIO() = default;
	GPIO(GPIO_NUM gpio) : gpio{ gpio } {}
	GPIO(GPIO_NUM gpio, Mode mode) : gpio{ gpio } { setMode(mode); }
	GPIO(GPIO_NUM gpio, Pull pull) : gpio{ gpio } { setPull(pull); }
	GPIO(GPIO_NUM gpio, Mode mode, Pull pull) : gpio{ gpio } { setMode(mode);setPull(pull); }
	GPIO(GPIO_NUM gpio, Mode mode, Pull pull, Interrupt interrupt, function_t function = nullptr, void* param = nullptr) : gpio{ gpio } { setMode(mode);setPull(pull);setInterrupt(interrupt, function, param); }

	GPIO(GPIO&) = default;
	GPIO& operator=(GPIO&) = default;
	GPIO(GPIO&&) = default;
	GPIO& operator=(GPIO&&) = default;
	~GPIO() = default;

	GPIO& operator=(bool level) { gpio_set_level(gpio, (int)level); return *this; }
	operator bool() { return gpio_get_level(gpio); }
	operator GPIO_NUM() { return gpio; }
	operator int() { return gpio; }

	void setMode(Mode mode) { gpio_set_direction(gpio, mode); }
	void setPull(Pull pull) { gpio_set_pull_mode(gpio, pull); }

	static void enableGlobalInterrupt() { gpio_install_isr_service(0); }
	static void disableGlobalInterrupt() { gpio_uninstall_isr_service(); }

	void setInterrupt(Interrupt interrupt, function_t function = nullptr, void* param = nullptr)
	{
		gpio_set_intr_type(gpio, interrupt);

		if (function == nullptr ||
			interrupt == Interrupt::GPIO_INTR_DISABLE)
			gpio_isr_handler_remove(gpio);
		else
			gpio_isr_handler_add(gpio, function, param);
	}

private:
	GPIO_NUM gpio = GPIO_NUM::GPIO_NUM_NC;
};
