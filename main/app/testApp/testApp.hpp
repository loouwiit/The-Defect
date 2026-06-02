#include "app/app.hpp"
#include "task/task.hpp"

class TestApp : public App
{
public:
	constexpr static char TAG[] = "TestApp";

	TestApp(Display* display);
	virtual ~TestApp();

	virtual void init() override;
	virtual void deinit() override;

private:
	lv_obj_t* fps{};
	Thread thread{};

	static void backgroundMain(void* param);
};
