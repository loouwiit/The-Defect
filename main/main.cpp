#include <esp_log.h>

#include "task/task.hpp"
#include "mutex/mutex.hpp"

extern "C" void app_main(void)
{
	constexpr static char TAG[] = "main";
	ESP_LOGI(TAG, "started");

	// 初始化自定义任务调度器（创建2个工作线程）
	Task::init(2);

	// 共享资源（必须为static以确保在app_main返回后仍然有效）
	static Mutex shared_mutex{};  // 保护共享变量的互斥锁
	static int shared_count{};    // 被多个任务并发访问的计数器
	static bool start_flag{};     // 控制任务开始执行的标志

	// 参数结构体，用于传递给自定义Task
	struct TaskParam
	{
		Mutex& mutex;
		int& count;
		bool& start;
	};
	static TaskParam task_params{ shared_mutex, shared_count, start_flag };

	// 创建10个自定义Task，每个Task会增加计数器10000次
	for (int i = 0; i < 10; i++)
	{
		Task::addTask([](void* param) -> TickType_t {
			constexpr static char TAG[] = "custom_task";

			auto& [mutex, count, start] = *(TaskParam*)param;

			// 等待开始信号
			if (!start) return 1; // 1ms后再次检查

			ESP_LOGI(TAG, "started with count %d", count);

			// 执行10000次原子递增操作
			for (int i = 0; i < 10000; i++)
			{
				Lock lock{ mutex }; // 自动获取和释放锁
				count++;
			}

			ESP_LOGI(TAG, "stopped with count %d", count);

			// 任务完成后永久休眠
			return Task::infinityTime;
			}, "custom_task", &task_params);
	}

	// 创建10个标准FreeRTOS线程，每个线程也会增加计数器10000次
	static Thread threads[10]{};
	struct ThreadParam
	{
		Thread& self;
		Mutex& mutex;
		int& count;
		bool& start;
	};

	for (auto& thread : threads)
	{
		thread = Thread{ [](void* param)
		{
			constexpr static char TAG[] = "freertos_thread";

			auto& [self, mutex, count, start] = *(ThreadParam*)param;

			ESP_LOGI(TAG, "started with count %d", count);
			vTaskDelay(0); // 强制调度

			// 执行10000次原子递增操作
			for (int i = 0; i < 10000; i++)
			{
				{
					Lock lock{mutex}; // 限制锁的作用域
					count++;
				}

				// 周期性让出CPU，避免饿死其他任务
				if (i % 0x40 == 0)
					vTaskDelay(0); // 强制调度
			}

			ESP_LOGI(TAG, "stopped with count %d", count);

			// 清理动态分配的参数
			delete (ThreadParam*)param;

			// 安全删除自身（通过析构函数）
			self = Thread{};

			// 终止当前任务执行
			vTaskDelete(NULL); // 可能是bug，析构函数应该会delete
		}, "freertos_thread", new ThreadParam{thread, shared_mutex, shared_count, start_flag} };
	}

	// 添加监控任务，每秒打印一次计数值
	Task::addTask([](void* param) -> TickType_t
		{
			constexpr static char TAG[] = "monitor";
			int value = *(int*)param;
			ESP_LOGI(TAG, "current count = %d", value);
			return 100; // 每100ms执行一次
		}, "monitor", &shared_count, 0, Task::Affinity::None);

	// 发送开始信号，所有任务开始执行
	start_flag = true;

	ESP_LOGI(TAG, "main function completed");
}
