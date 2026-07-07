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
	m_lives[0] = m_lives[1] = INITIAL_LIVES;
	m_timeRemaining = ARCADE_TIME;
	m_spawnTimer = 0;
	m_spawnInterval = BASE_SPAWN_INTERVAL;

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
			p.vy += GRAVITY * 0.3f * dt; // 粒子受轻量重力
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
		f.vy += GRAVITY * dt;
		f.rotation += f.rotSpeed * dt;

		// 已切片水果：两半飞离
		if (f.sliced)
		{
			f.h1x += f.h1vx * dt;
			f.h1y += f.h1vy * dt;
			f.h1vy += GRAVITY * 0.4f * dt;
			f.h2x += f.h2vx * dt;
			f.h2y += f.h2vy * dt;
			f.h2vy += GRAVITY * 0.4f * dt;
			f.halfOffset += static_cast<int>(dt * 120);

			if (f.opacity > 8)
				f.opacity -= static_cast<uint8_t>(dt * 200);
			else
				f.active = false;
			continue;
		}

		// 水果落出屏幕底部 → 扣命
		float r = getFruitRadius(f.type);
		if (f.y > SCREEN_H + r)
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
		p.vy += GRAVITY * 0.3f * dt;
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
			if (m_fruitCount >= MAX_FRUITS) return;

			auto& f = m_fruits[m_fruitCount];
			f = Fruit{};
			f.type = randomType();
			f.player = p;
			f.requiredDir = randomDir();
			f.requiredBtn = randomBtn();

			// 位置：从屏幕底部进入
			float r = getFruitRadius(f.type);
			if (m_playerCount == 1)
			{
				f.x = randFloat(r, SCREEN_W - r);
			}
			else
			{
				// 2P 分屏
				float halfW = SCREEN_W / 2.0f;
				if (p == 0)
					f.x = randFloat(r, halfW - r);
				else
					f.x = randFloat(halfW + r, SCREEN_W - r);
			}
			f.y = static_cast<float>(SCREEN_H) + r;

			// 速度：向上为主，带随机水平偏移
			float speed = randFloat(600, 1000);
			float angle = randFloat(-0.4f, 0.4f); // 弧度偏移
			f.vx = sinf(angle) * speed;
			f.vy = -speed * cosf(angle); // 向上

			f.rotSpeed = randFloat(-4.0f, 4.0f);
			f.active = true;

			m_fruitCount++;
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

	for (int i = 0; i < m_fruitCount; i++)
	{
		auto& f = m_fruits[i];
		if (!f.active || f.sliced) continue;
		if (f.player != player) continue;
		if (f.requiredDir != dir || f.requiredBtn != btn) continue;

		// 匹配成功！切片
		f.sliced = true;

		// 炸弹特殊处理
		if (f.type == FruitType::Bomb)
		{
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
		m_spawnInterval = BASE_SPAWN_INTERVAL - (totalScore / 30) * 0.05f;
		if (m_spawnInterval < MIN_SPAWN_INTERVAL)
			m_spawnInterval = MIN_SPAWN_INTERVAL;

		return true;
	}

	return false; // 无匹配水果
}

// ============================================================
// 触屏输入处理
// ============================================================

bool FruitNinjaLogic::handleTouch(int player, float tx, float ty)
{
	if (m_state != State::Playing) return false;
	if (player < 0 || player >= m_playerCount) return false;

	for (int i = 0; i < m_fruitCount; i++)
	{
		auto& f = m_fruits[i];
		if (!f.active || f.sliced) continue;
		if (f.player != player) continue;

		float r = getFruitRadius(f.type);
		float dx = tx - f.x;
		float dy = ty - f.y;
		if (dx * dx + dy * dy <= r * r)
		{
			// 触屏切片：效果同手柄匹配成功
			f.sliced = true;

			if (f.type == FruitType::Bomb)
			{
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
				return true;
			}

			switch (f.type)
			{
			case FruitType::Watermelon: m_scores[player] += 20; break;
			case FruitType::Apple:      m_scores[player] += 15; break;
			case FruitType::Orange:     m_scores[player] += 10; break;
			case FruitType::Banana:     m_scores[player] += 5;  break;
			default: break;
			}

			float r2 = getFruitRadius(f.type);
			f.h1x = f.x - r2 / 2;
			f.h1y = f.y;
			f.h1vx = -randFloat(80, 200);
			f.h1vy = randFloat(-200, -50);
			f.h2x = f.x + r2 / 2;
			f.h2y = f.y;
			f.h2vx = randFloat(80, 200);
			f.h2vy = randFloat(-200, -50);

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

			int totalScore = m_scores[0] + m_scores[1];
			m_spawnInterval = BASE_SPAWN_INTERVAL - (totalScore / 30) * 0.05f;
			if (m_spawnInterval < MIN_SPAWN_INTERVAL)
				m_spawnInterval = MIN_SPAWN_INTERVAL;

			return true;
		}
	}

	return false;
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
	for (int i = 0; i < count && m_particleCount < MAX_PARTICLES; i++)
	{
		auto& p = m_particles[m_particleCount];
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
		m_particleCount++;
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
