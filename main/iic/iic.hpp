#pragma once

#include "driver/i2c_master.h"
#include "gpio/gpio.hpp"

class IIC
{
public:
	using IicPort = i2c_port_num_t;
	constexpr static IicPort IicPortAuto = -1;

	IIC() = default;
	IIC(GPIO clock, GPIO data, IicPort port = IicPortAuto);
	~IIC();

	IIC(IIC&) = delete;
	IIC& operator=(IIC&) = delete;

	IIC(IIC&& move);
	IIC& operator=(IIC&& move);

	bool detect(uint16_t address);

private:
	friend class IICDevice;
	friend class Touch;

	i2c_master_bus_handle_t busHandle = nullptr;
};

class IICDevice
{
public:
	IICDevice() = default;
	IICDevice(IIC& iic, uint16_t address, unsigned speed = 100000);
	~IICDevice();

	IICDevice(IICDevice&) = delete;
	IICDevice& operator=(IICDevice&) = delete;

	IICDevice(IICDevice&& move);
	IICDevice& operator=(IICDevice&& move);

	bool detect();

	bool transmit(const char data);
	bool transmit(const void* buffer, size_t size);

	bool receive(void* buffer, size_t size);
	uint8_t receive();

	bool request(const void* write, size_t writeSize, void* read, size_t readSize);

private:
	IIC* iicBus = nullptr;
	uint16_t address{};
	i2c_master_dev_handle_t deviceHandle = nullptr;
};
