#include "iic.hpp"

#include <algorithm>

IIC::IIC(GPIO clock, GPIO data, IicPort port)
{
	i2c_master_bus_config_t busConfig{};

	busConfig.clk_source = i2c_clock_source_t::I2C_CLK_SRC_DEFAULT;
	busConfig.i2c_port = port;
	busConfig.scl_io_num = clock;
	busConfig.sda_io_num = data;
	busConfig.glitch_ignore_cnt = 7;
	busConfig.flags.enable_internal_pullup = true;

	ESP_ERROR_CHECK(i2c_new_master_bus(&busConfig, &busHandle));
}

IIC::~IIC()
{
	if (busHandle != nullptr)
		i2c_del_master_bus(busHandle);
}

IIC::IIC(IIC&& move)
{
	std::swap(move.busHandle, busHandle);
}

IIC& IIC::operator=(IIC&& move)
{
	std::swap(move.busHandle, busHandle);
	return *this;
}

bool IIC::detect(uint16_t address)
{
	return ESP_OK == i2c_master_probe(busHandle, address, 2);
}

IICDevice::IICDevice(IIC& iic, uint16_t address, unsigned speed) :
	iicBus{ &iic },
	address{ address }
{
	i2c_device_config_t dev_cfg{};

	dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
	dev_cfg.device_address = address;
	dev_cfg.scl_speed_hz = speed;

	ESP_ERROR_CHECK(i2c_master_bus_add_device(iic.busHandle, &dev_cfg, &deviceHandle));
}

IICDevice::~IICDevice()
{
	if (deviceHandle != nullptr)
		i2c_master_bus_rm_device(deviceHandle);
}

IICDevice::IICDevice(IICDevice&& move)
{
	std::swap(move.iicBus, iicBus);
	std::swap(move.address, address);
	std::swap(move.deviceHandle, deviceHandle);
}

IICDevice& IICDevice::operator=(IICDevice&& move)
{
	std::swap(move.iicBus, iicBus);
	std::swap(move.address, address);
	std::swap(move.deviceHandle, deviceHandle);
	return *this;
}

bool IICDevice::detect()
{
	return iicBus->detect(address);
}

bool IICDevice::transmit(const char data)
{
	return transmit(&data, 1);
}

bool IICDevice::transmit(const void* buffer, size_t size)
{

	return ESP_OK == i2c_master_transmit(deviceHandle, (const uint8_t*)buffer, size, 2);
}

bool IICDevice::receive(void* buffer, size_t size)
{
	return ESP_OK == i2c_master_receive(deviceHandle, (uint8_t*)buffer, size, 2);
}

uint8_t IICDevice::receive()
{
	uint8_t buffer = 0;
	receive(&buffer, 1);
	return buffer;
}

bool IICDevice::request(const void* write, size_t writeSize, void* read, size_t readSize)
{
	return ESP_OK == i2c_master_transmit_receive(deviceHandle, (const uint8_t*)write, writeSize, (uint8_t*)read, readSize, 2);
}
