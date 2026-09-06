#include "mixedInput.hpp"
#include "gamepad/virtualGamepad.hpp"

// ══════════════════════════════════════════════════════════════
//  Hat Switch 查找表
//  [垂直][水平] → hat 值
//  垂直: 0=无, 1=上, 2=下
//  水平: 0=无, 1=左, 2=右
// ══════════════════════════════════════════════════════════════

static constexpr uint8_t hatTable[3][3] = {
/*         无    左    右  */
/* 无 */ { 15,   6,    2 },
/* 上 */ {  0,   7,    1 },
/* 下 */ {  4,   5,    3 },
};

// ══════════════════════════════════════════════════════════════
//  MixedHidState 比较
// ══════════════════════════════════════════════════════════════

bool MixedHidState::operator==(const MixedHidState& o) const
{
	return x == o.x && y == o.y && rx == o.rx && ry == o.ry
		&& hat == o.hat && buttons == o.buttons;
}

// ══════════════════════════════════════════════════════════════
//  ABXY → Hat Switch 转换（Xbox 布局）
//    BTN_X=上, BTN_A=右, BTN_B=下, BTN_Y=左
// ══════════════════════════════════════════════════════════════

static uint8_t buttonsToHat(uint16_t abxy)
{
	bool up    = (abxy & BTN_X) != 0;
	bool down  = (abxy & BTN_B) != 0;
	bool left  = (abxy & BTN_Y) != 0;
	bool right = (abxy & BTN_A) != 0;

	// 对立方向抵消
	if (up && down)    up = down = false;
	if (left && right) left = right = false;

	uint8_t v = up ? 1 : (down ? 2 : 0);
	uint8_t h = left ? 1 : (right ? 2 : 0);

	return hatTable[v][h];
}

// ══════════════════════════════════════════════════════════════
//  按钮置换（swapAB: A↔B, swapXY: X↔Y）
// ══════════════════════════════════════════════════════════════

static uint16_t remapButtons(uint16_t abxy, bool swapAB, bool swapXY, bool swapABXY)
{
	uint16_t result = abxy;
	if (swapAB)
	{
		bool a = (result & BTN_A) != 0;
		bool b = (result & BTN_B) != 0;
		if (a) result |= BTN_B; else result &= ~BTN_B;
		if (b) result |= BTN_A; else result &= ~BTN_A;
	}
	if (swapXY)
	{
		bool x = (result & BTN_X) != 0;
		bool y = (result & BTN_Y) != 0;
		if (x) result |= BTN_Y; else result &= ~BTN_Y;
		if (y) result |= BTN_X; else result &= ~BTN_X;
	}
	if (swapABXY)
	{
		// A↔X, B↔Y（交叉对换）
		bool a = (result & BTN_A) != 0;
		bool x = (result & BTN_X) != 0;
		if (a) result |= BTN_X; else result &= ~BTN_X;
		if (x) result |= BTN_A; else result &= ~BTN_A;
		bool b = (result & BTN_B) != 0;
		bool y = (result & BTN_Y) != 0;
		if (b) result |= BTN_Y; else result &= ~BTN_Y;
		if (y) result |= BTN_B; else result &= ~BTN_B;
	}
	return result;
}

// ══════════════════════════════════════════════════════════════
//  mixInputs — 多源混合（friend 访问 MixedInputConfig 私有成员）
// ══════════════════════════════════════════════════════════════

MixedHidState mixInputs(const MixedInputConfig& config)
{
	MixedHidState state{};

	// 安全读取游戏手柄状态
	auto readGamepad = [](uint8_t hostId, uint8_t playerId) -> const GamepadState& {
		static const GamepadState empty{};
		if (hostId >= 4 || playerId >= 4) return empty;
		return VirtualGamepad::instance().getState(hostId, playerId);
	};

	// 提取 ABXY 四键位掩码
	auto extractAbxy = [](uint16_t buttons) -> uint16_t {
		return buttons & (BTN_A | BTN_B | BTN_X | BTN_Y);
	};

	// ── xy 轴 ──
	if (config.m_xySource.isValid())
	{
		const auto& gp = readGamepad(config.m_xySource.hostId, config.m_xySource.playerId);
		if (config.m_xySource.channel == SourceType::JOYSTICK)
		{
			state.x = static_cast<int8_t>(gp.lx - 128);
			state.y = static_cast<int8_t>(gp.ly - 128);
			if (config.m_xyInvertX) state.x = -state.x;
			if (config.m_xyInvertY) state.y = -state.y;
		}
	}

	// ── rxry 轴 ──
	if (config.m_rxrySource.isValid())
	{
		const auto& gp = readGamepad(config.m_rxrySource.hostId, config.m_rxrySource.playerId);
		if (config.m_rxrySource.channel == SourceType::JOYSTICK)
		{
			state.rx = static_cast<int8_t>(gp.lx - 128);
			state.ry = static_cast<int8_t>(gp.ly - 128);
			if (config.m_rxryInvertX) state.rx = -state.rx;
			if (config.m_rxryInvertY) state.ry = -state.ry;
		}
	}

	// ── dpad ──
	if (config.m_dpadSource.isValid())
	{
		const auto& gp = readGamepad(config.m_dpadSource.hostId, config.m_dpadSource.playerId);

		if (config.m_dpadSource.channel == SourceType::JOYSTICK)
		{
			// 摇杆 → hat（阈值判定）
			int8_t sx = static_cast<int8_t>(gp.lx - 128);
			int8_t sy = static_cast<int8_t>(gp.ly - 128);
			bool up    = sy < -config.m_joystickThreshold;
			bool down  = sy >  config.m_joystickThreshold;
			bool left  = sx < -config.m_joystickThreshold;
			bool right = sx >  config.m_joystickThreshold;
			if (up && down)    up = down = false;
			if (left && right) left = right = false;
			uint8_t v = up ? 1 : (down ? 2 : 0);
			uint8_t h = left ? 1 : (right ? 2 : 0);
			state.hat = hatTable[v][h];
		}
		else if (config.m_dpadSource.channel == SourceType::BUTTONS)
		{
			// 按钮 → hat（先置换再转换）
			uint16_t abxy = extractAbxy(gp.buttons);
			abxy = remapButtons(abxy, config.m_dpadSwapAB, config.m_dpadSwapXY, config.m_dpadSwapABXY);
			state.hat = buttonsToHat(abxy);
		}
	}

	// ── buttons ──
	if (config.m_buttonsSource.isValid())
	{
		const auto& gp = readGamepad(config.m_buttonsSource.hostId, config.m_buttonsSource.playerId);
		if (config.m_buttonsSource.channel == SourceType::BUTTONS)
		{
			uint16_t abxy = extractAbxy(gp.buttons);
			state.buttons = remapButtons(abxy, config.m_buttonsSwapAB, config.m_buttonsSwapXY, config.m_buttonsSwapABXY);
		}
	}

	return state;
}

// ══════════════════════════════════════════════════════════════
//  buildHidReport — 序列化为 HID 报文字节
//  报文格式 (7 bytes):
//    byte 0~1: buttons[15] + padding[1] (uint16, 小端序)
//    byte 2:   X  (int8)
//    byte 3:   Y  (int8)
//    byte 4:   RX (int8)
//    byte 5:   RY (int8)
//    byte 6:   Hat(4bit) + padding(4bit)
// ══════════════════════════════════════════════════════════════

void buildHidReport(const MixedHidState& state, uint8_t out[kHidReportSize])
{
	out[0] = static_cast<uint8_t>(state.buttons & 0xFF);
	out[1] = static_cast<uint8_t>((state.buttons >> 8) & 0x7F);
	out[2] = static_cast<uint8_t>(state.x);
	out[3] = static_cast<uint8_t>(state.y);
	out[4] = static_cast<uint8_t>(state.rx);
	out[5] = static_cast<uint8_t>(state.ry);
	out[6] = state.hat & 0x0F;
}
