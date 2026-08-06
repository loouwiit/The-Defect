#include "app/app.hpp"
#include "task/task.hpp"
#include <cstdint>

class TestApp : public App
{
public:
	constexpr static char TAG[] = "TestApp";

	/** @param display  Display 指针
	 *  @param value     显示在标签上的随机值，由父 app 传入 */
	TestApp(Display* display, uint32_t value);
	virtual ~TestApp();

	static App* newApp(Display* display, const cJSON* params)
	{
		(void)params;
		return new TestApp{ display, (unsigned)rand() };
	}

	virtual void init() override;
	virtual void deinit() override;

	virtual void onForeground() override;

	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	uint32_t m_value{};
	lv_obj_t* m_label{};
	lv_obj_t* m_hint{};
	lv_obj_t* m_touchInfo{};
	lv_obj_t* m_dots[5]{};
	Task* m_touchTask{};

	TickType_t nextAppChangeTime{};

	// —— 多指可视化试验田 ——
	void startTouchDemo();
	void stopTouchDemo();
	static TickType_t touchDemoTick(void* param);
	void updateTouchDemo();
};
