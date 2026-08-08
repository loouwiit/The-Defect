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
	// ── GT911 多指可视化试验台 ──
	struct FingerMarker
	{
		lv_obj_t* dot{};
		lv_obj_t* tag{};
	} m_markers[5];

	Task* m_tick{};

	static TickType_t tickCb(void* param);
	void refresh();

	uint32_t m_value{};
	TickType_t nextAppChangeTime{};

	// ── ELF 测试面板 ──
	lv_obj_t* m_elfStatus{};       // 模块状态标签
	int m_pendingAction{};         // 待执行动作（延迟任务用）
	static constexpr const char* SMOKE_PATH = "/root/elf/smoke.so";
	void refreshElfPanel();        // 刷新状态标签
	void elfAction(int action);    // 0=加载 1=运行 2=卸载 3=重载
	static void onElfBtnCb(lv_event_t* e);
};
