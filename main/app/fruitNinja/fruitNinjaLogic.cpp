#include "fruitNinjaLogic.hpp"
#include <cmath>
#include <cstring>
#include "esp_log.h"

static constexpr char TAG[] = "FruitNinjaLogic";

// ============================================================
// 构造
// ============================================================

FruitNinjaLogic::FruitNinjaLogic()
{
	srand(time(nullptr));
	reset();
}

void FruitNinjaLogic::setPlayerCount(int count)
{
	m_playerCount = (count < 1) ? 1 : (count > 2) ? 2 : count;
}

// ============================================================
// 重置
// ============================================================

void FruitNinjaLogic::reset()
{
	m_state = State::Waiting;
	m_fruitCount = 0;
	m_particleCount = 0;
	m_scores[0] = m_scores[1] = 0;
	m_lives[0] = m_lives[1] = InitialLives;
	m_timeRemaining = ArcadeTime;
	m_spawnTimer = 0;
	m_spawnInterval = SpawnIntervalBase;

	for (auto& f : m_fruits)
		f = Fruit{};
	for (auto& p : m_particles)
		p = Particle{};
}

// ============================================================
// 游戏主 tick
// ============================================================

void FruitNinjaLogic::tick(float dt)
{
	if (m_state != State::Playing)
	{
		// 仍在更新粒子动画
		for (int i = 0; i < m_particleCount; i++)
		{
			auto& p = m_particles[i];
			if (!p.active) continue;
			p.x += p.vx * dt;
			p.y += p.vy * dt;
			p.vy += Gravity * 0.3f * dt; // 粒子受轻量重力
			if (p.opacity > 10)
				p.opacity -= static_cast<uint8_t>(dt * 300);
			else
				p.active = false;
		}
		return;
	}

	// ── Arcade 计时 ──
	if (m_mode == GameMode::Arcade)
	{
		m_timeRemaining -= dt;
		if (m_timeRemaining <= 0)
		{
			m_timeRemaining = 0;
			m_state = State::GameOver;
			return;
		}
	}

	// ── 更新水果物理 ──
	for (int i = 0; i < m_fruitCount; i++)
	{
		auto& f = m_fruits[i];
		if (!f.active) continue;

		// 物理
		f.x += f.vx * dt;
		f.y += f.vy * dt;
		f.vy += Gravity * dt;
		f.rotation += f.rotSpeed * dt;

		// 已切片水果：两半飞离
		if (f.sliced)
		{
			f.h1x += f.h1vx * dt;
			f.h1y += f.h1vy * dt;
			f.h1vy += Gravity * 0.4f * dt;
			f.h2x += f.h2vx * dt;
			f.h2y += f.h2vy * dt;
			f.h2vy += Gravity * 0.4f * dt;
			f.halfOffset += static_cast<int>(dt * 120);

			if (f.opacity > 8)
				f.opacity -= static_cast<uint8_t>(dt * 200);
			else
				f.active = false;
			continue;
		}

		// 横向出界：水果整体离开屏幕水平范围，玩家无从切片 → 直接回收，不扣分
		float r = getFruitRadius(f.type);
		if (f.x + r < 0 || f.x - r > ScreenWidth)
		{
			f.active = false;
			continue;
		}

		// 水果落出屏幕底部 → 扣命（玩家看到但未切到的惩罚）
		if (f.y > ScreenHight + r)
		{
			f.active = false;
			if (f.type != FruitType::Bomb) // 炸弹没切到是好事
			{
				if (m_mode == GameMode::Classic)
				{
					m_lives[f.player]--;
					if (m_lives[f.player] <= 0)
					{
						m_lives[f.player] = 0;
						m_state = State::GameOver;
						return;
					}
				}
				else // Arcade
				{
					m_scores[f.player] -= 5;
					if (m_scores[f.player] < 0) m_scores[f.player] = 0;
				}
			}
		}
	}

	// ── 生成新水果 ──
	m_spawnTimer -= dt;
	if (m_spawnTimer <= 0)
	{
		spawnWave();
		m_spawnTimer = m_spawnInterval;
	}

	// ── 更新粒子 ──
	for (int i = 0; i < m_particleCount; i++)
	{
		auto& p = m_particles[i];
		if (!p.active) continue;
		p.x += p.vx * dt;
		p.y += p.vy * dt;
		p.vy += Gravity * 0.3f * dt;
		if (p.opacity > 10)
			p.opacity -= static_cast<uint8_t>(dt * 350);
		else
			p.active = false;
	}
}

// ============================================================
// 生成水果波
// ============================================================

void FruitNinjaLogic::spawnWave()
{
	int fruitsPerPlayer = randInt(1, 2);

	for (int p = 0; p < m_playerCount; p++)
	{
		for (int n = 0; n < fruitsPerPlayer; n++)
		{
			// 槽位复用：扫描第一个空闲槽（被切片/落屏的水果仅置 active=false，
			// 槽位不回收会导致累计 40 个水果后永久不再生成）。MaxFruits = 同时活跃上限。
			int slot = -1;
			for (int i = 0; i < MaxFruits; i++)
			{
				if (!m_fruits[i].active) { slot = i; break; }
			}
			if (slot < 0) return; // 全部槽位被活跃水果占用，等待现有水果被处理

			auto& f = m_fruits[slot];
			f = Fruit{};
			f.type = randomType();
			f.player = p;
			f.requiredDir = randomDir();
			f.requiredBtn = randomBtn();

			// 位置：从屏幕底部进入
			float r = getFruitRadius(f.type);
			if (m_playerCount == 1)
			{
				f.x = randFloat(r, ScreenWidth - r);
			}
			else
			{
				// 2P 分屏
				float halfW = ScreenWidth / 2.0f;
				if (p == 0)
					f.x = randFloat(r, halfW - r);
				else
					f.x = randFloat(halfW + r, ScreenWidth - r);
			}
			f.y = static_cast<float>(ScreenHight) + r;

			// 速度：向上为主，带随机水平偏移
			float speed = randFloat(SpeedRange[0], SpeedRange[1]);
			float angle = randFloat(AngelRange[0], AngelRange[1]); // 弧度偏移
			f.vx = sinf(angle) * speed;
			f.vy = -speed * cosf(angle); // 向上

			f.rotSpeed = randFloat(-4.0f, 4.0f);
			f.active = true;

			// m_fruitCount = 最高使用槽位+1（非递减，封顶 MAX_FRUITS，渲染器以 `i < fruitCount` 判活）
			if (slot + 1 > m_fruitCount)
				m_fruitCount = slot + 1;
		}
	}
}

// ============================================================
// 手柄输入处理
// ============================================================

bool FruitNinjaLogic::handleGamepadInput(int player, Direction dir, Button btn)
{
	if (m_state != State::Playing) return false;
	if (player < 0 || player >= m_playerCount) return false;

	// 第一遍：优先精确匹配正常水果（方向 + 特定按钮）；命中即切片，不引爆炸弹
	for (int i = 0; i < m_fruitCount; i++)
	{
		auto& f = m_fruits[i];
		if (!f.active || f.sliced) continue;
		if (f.player != player) continue;
		if (f.type == FruitType::Bomb) continue;   // 炸弹延迟到第二遍判定
		if (f.requiredDir != dir || f.requiredBtn != btn) continue;

		// 匹配成功！切片
		f.sliced = true;

		// 正常水果：加分
		switch (f.type)
		{
		case FruitType::Watermelon: m_scores[player] += 20; break;
		case FruitType::Apple:      m_scores[player] += 15; break;
		case FruitType::Orange:     m_scores[player] += 10; break;
		case FruitType::Banana:     m_scores[player] += 5;  break;
		default: break;
		}

		// 初始化两半飞离动画
		float r = getFruitRadius(f.type);
		f.h1x = f.x - r / 2;
		f.h1y = f.y;
		f.h1vx = -randFloat(80, 200);
		f.h1vy = randFloat(-200, -50);
		f.h2x = f.x + r / 2;
		f.h2y = f.y;
		f.h2vx = randFloat(80, 200);
		f.h2vy = randFloat(-200, -50);

		// 水果颜色粒子
		auto colorOf = [](FruitType t) -> uint8_t {
			switch (t) {
			case FruitType::Watermelon: return 0;    // green
			case FruitType::Apple:      return 255;  // red-ish
			case FruitType::Orange:     return 255;  // orange
			case FruitType::Banana:     return 255;  // yellow
			default: return 200;
			}
			};
		spawnParticles(f.x, f.y, colorOf(f.type), 200, 80, 8);

		// 根据分数调整生成间隔
		int totalScore = m_scores[0] + m_scores[1];
		m_spawnInterval = SpawnIntervalBase - (totalScore / 30) * 0.05f;
		if (m_spawnInterval < SpawnIntervalMin)
			m_spawnInterval = SpawnIntervalMin;

		return true;
	}

	// 第二遍：水果匹配失败 → 判定炸弹引爆（炸弹只要求方向匹配，任意按钮均引爆，提高难度）
	for (int i = 0; i < m_fruitCount; i++)
	{
		auto& f = m_fruits[i];
		if (!f.active || f.sliced) continue;
		if (f.player != player) continue;
		if (f.type != FruitType::Bomb) continue;
		if (f.requiredDir != dir) continue;

		// 炸弹引爆（惩罚）
		if (m_mode == GameMode::Classic)
		{
			m_lives[player]--;
			if (m_lives[player] <= 0)
			{
				m_lives[player] = 0;
				m_state = State::GameOver;
			}
		}
		else // Arcade
		{
			m_scores[player] -= 10;
			if (m_scores[player] < 0) m_scores[player] = 0;
		}
		f.active = false;
		spawnParticles(f.x, f.y, 255, 80, 80, 12); // 红色爆炸
		return true;
	}

	return false; // 无匹配水果/炸弹
}

// ============================================================
// 线段与圆相交检测
// ============================================================

bool FruitNinjaLogic::lineIntersectsCircle(float x1, float y1, float x2, float y2, float cx, float cy, float r)
{
	// 向量 AB = B - A, AC = C - A
	float abx = x2 - x1;
	float aby = y2 - y1;
	float acx = cx - x1;
	float acy = cy - y1;

	// 线段长度平方
	float ab2 = abx * abx + aby * aby;
	if (ab2 < 0.0001f)
	{
		// 线段退化为点，用点-圆检测
		float dx = cx - x1;
		float dy = cy - y1;
		return (dx * dx + dy * dy) <= r * r;
	}

	// 投影参数 t，夹紧到 [0,1]
	float t = (acx * abx + acy * aby) / ab2;
	t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;

	// 线段上最近点
	float px = x1 + t * abx;
	float py = y1 + t * aby;

	// 圆心到最近点的距离
	float dx = cx - px;
	float dy = cy - py;
	return (dx * dx + dy * dy) <= r * r;
}

// ============================================================
// 触屏划动输入处理（一刀可切多个水果）
// ============================================================

bool FruitNinjaLogic::handleSwipe(int player, float x1, float y1, float x2, float y2)
{
	if (m_state != State::Playing) return false;
	if (player < 0 || player >= m_playerCount) return false;

	bool anySliced = false;

	for (int i = 0; i < m_fruitCount; i++)
	{
		auto& f = m_fruits[i];
		if (!f.active || f.sliced) continue;
		if (f.player != player) continue;

		float r = getFruitRadius(f.type);
		if (!lineIntersectsCircle(x1, y1, x2, y2, f.x, f.y, r))
			continue;

		// 划动命中！切片
		f.sliced = true;
		anySliced = true;

		if (f.type == FruitType::Bomb)
		{
			// 炸弹特殊处理
			if (m_mode == GameMode::Classic)
			{
				m_lives[player]--;
				if (m_lives[player] <= 0)
				{
					m_lives[player] = 0;
					m_state = State::GameOver;
				}
			}
			else
			{
				m_scores[player] -= 10;
				if (m_scores[player] < 0) m_scores[player] = 0;
			}
			f.active = false;
			spawnParticles(f.x, f.y, 255, 80, 80, 12);
			continue;
		}

		// 正常水果：加分
		switch (f.type)
		{
		case FruitType::Watermelon: m_scores[player] += 20; break;
		case FruitType::Apple:      m_scores[player] += 15; break;
		case FruitType::Orange:     m_scores[player] += 10; break;
		case FruitType::Banana:     m_scores[player] += 5;  break;
		default: break;
		}

		// 初始化两半飞离动画
		float fr = getFruitRadius(f.type);
		f.h1x = f.x - fr / 2;
		f.h1y = f.y;
		f.h1vx = -randFloat(80, 200);
		f.h1vy = randFloat(-200, -50);
		f.h2x = f.x + fr / 2;
		f.h2y = f.y;
		f.h2vx = randFloat(80, 200);
		f.h2vy = randFloat(-200, -50);

		// 水果颜色粒子
		auto colorOf = [](FruitType t) -> uint8_t {
			switch (t) {
			case FruitType::Watermelon: return 0;
			case FruitType::Apple:      return 255;
			case FruitType::Orange:     return 255;
			case FruitType::Banana:     return 255;
			default: return 200;
			}
			};
		spawnParticles(f.x, f.y, colorOf(f.type), 200, 80, 8);
	}

	// 一刀至少切了一个水果才调整生成间隔
	if (anySliced)
	{
		int totalScore = m_scores[0] + m_scores[1];
		m_spawnInterval = SpawnIntervalBase - (totalScore / 30) * 0.05f;
		if (m_spawnInterval < SpawnIntervalMin)
			m_spawnInterval = SpawnIntervalMin;
	}

	return anySliced;
}

// ============================================================
// 数据查询
// ============================================================

int FruitNinjaLogic::getScore(int player) const
{
	if (player < 0 || player >= 2) return 0;
	return m_scores[player];
}

int FruitNinjaLogic::getLives(int player) const
{
	if (player < 0 || player >= 2) return 0;
	return m_lives[player];
}

float FruitNinjaLogic::getFruitRadius(FruitType type)
{
	switch (type)
	{
	case FruitType::Watermelon: return 40.0f;
	case FruitType::Apple:      return 30.0f;
	case FruitType::Orange:     return 28.0f;
	case FruitType::Banana:     return 22.0f;
	case FruitType::Bomb:       return 32.0f;
	default:                    return 30.0f;
	}
}

const char* FruitNinjaLogic::dirToArrow(Direction d)
{
	switch (d)
	{
	case Direction::Up:    return "\u2191"; // ↑
	case Direction::Down:  return "\u2193"; // ↓
	case Direction::Left:  return "\u2190"; // ←
	case Direction::Right: return "\u2192"; // →
	default:               return "?";
	}
}

const char* FruitNinjaLogic::btnToChar(Button b)
{
	switch (b)
	{
	case Button::A: return "A";
	case Button::B: return "B";
	case Button::X: return "X";
	case Button::Y: return "Y";
	default:        return "?";
	}
}

// ============================================================
// 粒子
// ============================================================

void FruitNinjaLogic::spawnParticles(float x, float y, uint8_t r, uint8_t g, uint8_t b, int count)
{
	// 槽位复用：扫描第一个空闲粒子槽（死亡粒子仅置 active=false，
	// 槽位不回收会导致累计 40 个粒子后效果永久消失）。MaxParticles = 同时活跃上限。
	int spawned = 0;
	for (int i = 0; i < MaxParticles && spawned < count; i++)
	{
		auto& p = m_particles[i];
		if (p.active) continue;

		p.x = x + randFloat(-15, 15);
		p.y = y + randFloat(-15, 15);
		float speed = randFloat(100, 400);
		float angle = randFloat(0, 6.2832f);
		p.vx = cosf(angle) * speed;
		p.vy = sinf(angle) * speed;
		p.r = r;
		p.g = g;
		p.b = b;
		p.size = static_cast<uint8_t>(randInt(3, 8));
		p.opacity = 255;
		p.active = true;
		spawned++;

		// m_particleCount = 最高使用槽位+1（非递减，封顶 MAX_PARTICLES，渲染器以 `i < particleCount` 判活）
		if (i + 1 > m_particleCount)
			m_particleCount = i + 1;
	}
}

// ============================================================
// 随机工具
// ============================================================

float FruitNinjaLogic::randFloat(float min, float max)
{
	return min + (max - min) * (static_cast<float>(rand()) / RAND_MAX);
}

int FruitNinjaLogic::randInt(int min, int max)
{
	return min + rand() % (max - min + 1);
}

FruitNinjaLogic::Direction FruitNinjaLogic::randomDir()
{
	return static_cast<Direction>(randInt(0, 3));
}

FruitNinjaLogic::Button FruitNinjaLogic::randomBtn()
{
	return static_cast<Button>(randInt(0, 3));
}

FruitNinjaLogic::FruitType FruitNinjaLogic::randomType()
{
	// 15% 概率为炸弹
	if (rand() % 100 < 15)
		return FruitType::Bomb;
	return static_cast<FruitType>(randInt(0, 3));
}
