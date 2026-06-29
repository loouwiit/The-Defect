#pragma once

#include <cstdint>

/**
 * @brief 游戏操作抽象
 *
 * 遵循 README 中的抽象设计原则：
 * "将输入抽象。一个输入对于一个 inputer。"
 *
 * clickAble — 触摸输入，内部三个指针（down/move/up）
 * focusAble — 方向键输入（绑定到玩家），内部四个指针（上/下/左/右）
 */
enum class GameAction : uint8_t
{
	None,
	Up,
	Down,
	Left,
	Right,
	Confirm,  // 确定/开始
	Cancel,   // 取消/返回
};

/**
 * @brief Inputer 抽象基类
 *
 * 每个 Inputer 对应一个玩家的输入源。
 * 派生类可代表：
 *   - 触摸屏上的虚拟方向键 (ClickAble)
 *   - 硬件方向键 (FocusAble)
 *   - WebSocket 远程输入 (RemoteInputer)
 *   - ESP-NOW 手柄输入
 */
class Inputer
{
public:
	virtual ~Inputer() = default;

	/** @brief 获取当前输入动作（非阻塞） */
	virtual GameAction getAction() = 0;

	/** @brief 重置输入状态（如游戏重启时清除遗留输入） */
	virtual void reset() {}
};

/**
 * @brief ClickAble — 触摸输入抽象
 *
 * 内部三个"指针"方法：
 *   1. onTouchDown(point) — 触摸按下
 *   2. onTouchMove(point) — 触摸移动
 *   3. onTouchUp(point)   — 触摸释放
 *
 * 适用于触屏游戏，将触摸位置映射为方向/操作。
 */
class ClickAble
{
public:
	virtual ~ClickAble() = default;

	/** @brief 触摸按下 */
	virtual void onTouchDown(int16_t x, int16_t y) = 0;

	/** @brief 触摸移动 */
	virtual void onTouchMove(int16_t x, int16_t y) = 0;

	/** @brief 触摸释放 */
	virtual void onTouchUp(int16_t x, int16_t y) = 0;
};

/**
 * @brief FocusAble — 方向键输入抽象
 *
 * 内部四个"指针"方法：
 *   1. onUp()    — 上方向
 *   2. onDown()  — 下方向
 *   3. onLeft()  — 左方向
 *   4. onRight() — 右方向
 *
 * 可绑定到：
 *   - 物理按钮（通过 GPIO 中断）
 *   - 虚拟 D-pad（通过 LVGL 事件）
 *   - 远程 WebSocket 方向事件
 */
class FocusAble
{
public:
	virtual ~FocusAble() = default;

	virtual void onUp()    = 0;
	virtual void onDown()  = 0;
	virtual void onLeft()  = 0;
	virtual void onRight() = 0;
	virtual void onConfirm() = 0;
	virtual void onCancel()  = 0;
};

/**
 * @brief SoundAble — 音效抽象
 *
 * 为游戏提供音效播放能力。
 */
class SoundAble
{
public:
	virtual ~SoundAble() = default;
	virtual void playEat()    = 0;  // 吃食物音效
	virtual void playDie()    = 0;  // 死亡音效
	virtual void playTurn()   = 0;  // 转向音效
	virtual void playStart()  = 0;  // 开始音效
};

// ============================================================
// 具体实现
// ============================================================

/**
 * @brief DirectionKeyInputer — 通过方向键/按钮产生 GameAction
 *
 * 继承 Inputer + FocusAble。外部按键调用 FocusAble 方法，
 * getAction() 消费队列中的最新动作。
 */
class DirectionKeyInputer : public Inputer, public FocusAble
{
public:
	GameAction getAction() override;
	void reset() override;

	// FocusAble
	void onUp()    override;
	void onDown()  override;
	void onLeft()  override;
	void onRight() override;
	void onConfirm() override;
	void onCancel()  override;

private:
	static constexpr int QUEUE_SIZE = 4;
	GameAction m_queue[QUEUE_SIZE]{};
	int m_head = 0;
	int m_tail = 0;

	void push(GameAction action);
};

/**
 * @brief TouchSwipeInputer — 通过触摸滑动产生方向
 *
 * 继承 Inputer + ClickAble。触摸 down 记录起始点，
 * 触摸 up 时计算滑动方向并放入队列。
 * 也支持点击 D-pad 区域触发方向。
 */
class TouchSwipeInputer : public Inputer, public ClickAble
{
public:
	TouchSwipeInputer();
	~TouchSwipeInputer() = default;

	GameAction getAction() override;
	void reset() override;

	// ClickAble
	void onTouchDown(int16_t x, int16_t y) override;
	void onTouchMove(int16_t x, int16_t y) override;
	void onTouchUp(int16_t x, int16_t y) override;

	/** @brief 设置 D-pad 按钮的 LVGL 区域（px 坐标） */
	void setDpadZone(int16_t cx, int16_t cy, int16_t halfSize);

private:
	int16_t m_downX = 0, m_downY = 0;
	int16_t m_dpadCX = 0, m_dpadCY = 0, m_dpadHalf = 0;
	bool m_touching = false;
	bool m_inDpad = false;

	static constexpr int QUEUE_SIZE = 4;
	GameAction m_queue[QUEUE_SIZE]{};
	int m_head = 0;
	int m_tail = 0;
	static constexpr int SWIPE_THRESHOLD = 30; // px

	void push(GameAction action);
	GameAction posToDir(int16_t x, int16_t y) const;
};
