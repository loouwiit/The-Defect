#include "app/app.hpp"
#include <cstdint>

class TestApp : public App
{
public:
	constexpr static char TAG[] = "TestApp";

	/** @param display  Display 指针
	 *  @param value     显示在标签上的随机值，由父 app 传入 */
	TestApp(Display* display, uint32_t value);
	virtual ~TestApp();

	virtual void init() override;
	virtual void deinit() override;

	virtual void onForeground() override;

	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	uint32_t m_value{};
	lv_obj_t* m_label{};
	lv_obj_t* m_hint{};

	TickType_t nextAppChangeTime{};
};
