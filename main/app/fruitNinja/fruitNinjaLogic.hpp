#pragma once

#include <cstdint>
#include <cstdlib>
#include <ctime>

/**
 * @brief 水果忍者游戏核心逻辑（纯 C++，无 LVGL 依赖）
 *
 * 核心玩法：每个水果带有方向（↑↓←→）+ 按钮（A/B/X/Y）提示，
 * 玩家须推动左摇杆到正确方向 + 按下对应按钮才可切片。
 * 触屏可直接点按水果切片（简化操作）。
 */
class FruitNinjaLogic
{
public:
	// ── 枚举 ──
	enum class Direction : uint8_t { Up, Down, Left, Right };
	enum class Button : uint8_t { A, B, X, Y };
	enum class FruitType : uint8_t { Watermelon, Apple, Orange, Banana, Bomb };
	enum class GameMode : uint8_t { Classic, Arcade };
	enum class State : uint8_t { Waiting, Playing, Paused, GameOver };

	// ── 水果数据 ──
	struct Fruit
	{
		float x = 0, y = 0;           // 像素坐标
		float vx = 0, vy = 0;         // 速度 (px/s)
		float rotation = 0;           // 当前旋转角度
		float rotSpeed = 0;           // 旋转速度 (rad/s)
		FruitType type = FruitType::Watermelon;
		Direction requiredDir = Direction::Up;
		Button requiredBtn = Button::A;
		uint8_t player = 0;           // 0=P1, 1=P2
		bool active = false;
		bool sliced = false;
		int halfOffset = 0;           // 切片后两半分离偏移
		uint8_t opacity = 255;        // 淡出用 0~255

		// 切片动画用
		float h1x = 0, h1y = 0, h1vx = 0, h1vy = 0;
		float h2x = 0, h2y = 0, h2vx = 0, h2vy = 0;
	};

	// ── 粒子数据（供渲染器读取） ──
	struct Particle
	{
		float x = 0, y = 0;
		float vx = 0, vy = 0;
		uint8_t r = 0, g = 0, b = 0;  // 颜色
		uint8_t size = 4;             // 直径 px
		uint8_t opacity = 255;
		bool active = false;
	};

	// ── 常量 ──
	static constexpr int MaxFruits = 20;      // 同时活跃水果上限（槽位池回收复用）
	static constexpr int MaxParticles = 40;   // 同时活跃粒子上限（槽位池回收复用）
	static constexpr int InitialLives = 3;
	static constexpr float Gravity = 300.0f;
	static constexpr float SpeedRange[2] = { 300.0f, 600.0f };
	static constexpr float AngelRange[2] = { -0.4f, 0.4f };
	static constexpr int ArcadeTime = 60;
	static constexpr int ScreenWidth = 1280;
	static constexpr int ScreenHight = 720;
	static constexpr float SpawnIntervalBase = 1.2f;
	static constexpr float SpawnIntervalMin = 0.5f;

	FruitNinjaLogic();
	~FruitNinjaLogic() = default;

	// ── 配置 ──
	void setMode(GameMode mode) { m_mode = mode; }
	void setPlayerCount(int count);
	GameMode getMode() const { return m_mode; }
	int getPlayerCount() const { return m_playerCount; }

	// ── 状态 ──
	State getState() const { return m_state; }
	void setState(State s) { m_state = s; }
	int getScore(int player) const;
	int getLives(int player) const;
	float getTimeRemaining() const { return m_timeRemaining; }

	// ── 核心操作 ──
	void reset();
	void tick(float dt);

	/** @brief 手柄输入：方向+按钮匹配。返回 true=切片成功 */
	bool handleGamepadInput(int player, Direction dir, Button btn);

	/** @brief 触屏输入：划动线段检测。返回 true=至少一个水果被切片 */
	bool handleSwipe(int player, float x1, float y1, float x2, float y2);

	// ── 数据读取（供渲染器） ──
	int getFruitCount() const { return m_fruitCount; }
	const Fruit* getFruits() const { return m_fruits; }
	int getParticleCount() const { return m_particleCount; }
	const Particle* getParticles() const { return m_particles; }

	/** @brief 水果碰撞体半径 */
	static float getFruitRadius(FruitType type);

	/** @brief 线段与圆相交检测（用于划动判定） */
	static bool lineIntersectsCircle(float x1, float y1, float x2, float y2, float cx, float cy, float r);

	/** @brief 方向→箭头字符 */
	static const char* dirToArrow(Direction d);
	/** @brief 按钮→字符 */
	static const char* btnToChar(Button b);

private:
	// ── 成员 ──
	Fruit m_fruits[MaxFruits];
	int m_fruitCount = 0;        // 最高使用槽位+1（非递减，封顶 MAX_FRUITS）；渲染器以 `i < m_fruitCount && active` 判活
	Particle m_particles[MaxParticles];
	int m_particleCount = 0;     // 最高使用槽位+1（非递减，封顶 MAX_PARTICLES）；渲染器以 `i < m_particleCount && active` 判活

	int m_playerCount = 1;
	GameMode m_mode = GameMode::Classic;
	State m_state = State::Waiting;

	int m_scores[2] = { 0, 0 };
	int m_lives[2] = { InitialLives, InitialLives };
	float m_timeRemaining = ArcadeTime;

	float m_spawnTimer = 0;
	float m_spawnInterval = SpawnIntervalBase;

	// ── 内部方法 ──
	void spawnWave();
	void spawnParticles(float x, float y, uint8_t r, uint8_t g, uint8_t b, int count);
	float randFloat(float min, float max);
	int randInt(int min, int max);
	Direction randomDir();
	Button randomBtn();
	FruitType randomType();
};
