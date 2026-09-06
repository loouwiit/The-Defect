#pragma once

#include <cstdint>
#include <cstddef>
#include "gamepadState.hpp"

// ══════════════════════════════════════════════════════════════
//  混合输入 — 将多个 BLE 手柄的不同通道合并为单一 HID 报文
// ══════════════════════════════════════════════════════════════

/// 源通道类型
enum class SourceType : uint8_t {
	JOYSTICK,   // 模拟摇杆: lx, ly
	BUTTONS,    // 面板按钮: ABXY 位掩码
};

/// 数据源引用 — 指明某个手柄的某个通道
/// hostId >= 4 表示无效源（mixInputs 跳过）
struct Source {
	uint8_t hostId = 0xFF;  // 0xFF = 无效（>= 4）
	uint8_t playerId = 0;
	SourceType channel = SourceType::JOYSTICK;

	bool isValid() const { return hostId < 4 && playerId < 4; }
};

/// HID 输出状态（混合后的目标状态）
struct MixedHidState;

/// 混合输入配置 — 每个 HID 字段指定其唯一数据源（target-centric）
class MixedInputConfig {
private:
	Source m_xySource;
	bool m_xyInvertX = false, m_xyInvertY = false;

	Source m_rxrySource;
	bool m_rxryInvertX = false, m_rxryInvertY = false;

	Source m_dpadSource;
	bool m_dpadSwapAB = false, m_dpadSwapXY = false, m_dpadSwapABXY = false;

	Source m_buttonsSource;
	bool m_buttonsSwapAB = false, m_buttonsSwapXY = false, m_buttonsSwapABXY = false;

	uint8_t m_joystickThreshold = 80;
	bool m_valid = true;

	friend MixedHidState mixInputs(const MixedInputConfig& config);

public:
	constexpr MixedInputConfig() = default;

	/// 设置主摇杆源 (X/Y)
	constexpr MixedInputConfig& setXYSource(const Source& source,
		bool invertX = false, bool invertY = false)
	{
		m_xySource = source;
		m_xyInvertX = invertX;
		m_xyInvertY = invertY;
		return *this;
	}

	/// 设置副摇杆源 (RX/RY)
	constexpr MixedInputConfig& setRXRYSource(const Source& source,
		bool invertX = false, bool invertY = false)
	{
		m_rxrySource = source;
		m_rxryInvertX = invertX;
		m_rxryInvertY = invertY;
		return *this;
	}

	/// 设置 D-Pad 源（支持 JOYSTICK 或 BUTTONS）
	/// swapAB=A↔B, swapXY=X↔Y, swapABXY=A↔X+B↔Y，三者组合可实现任意排列
	constexpr MixedInputConfig& setDpadSource(const Source& source,
		bool swapAB = false, bool swapXY = false, bool swapABXY = false)
	{
		m_dpadSource = source;
		m_dpadSwapAB = swapAB;
		m_dpadSwapXY = swapXY;
		m_dpadSwapABXY = swapABXY;
		return *this;
	}

	/// 设置 Buttons 源
	constexpr MixedInputConfig& setButtonsSource(const Source& source,
		bool swapAB = false, bool swapXY = false, bool swapABXY = false)
	{
		m_buttonsSource = source;
		m_buttonsSwapAB = swapAB;
		m_buttonsSwapXY = swapXY;
		m_buttonsSwapABXY = swapABXY;
		return *this;
	}

	/// 设置摇杆→D-Pad 方向判定阈值 (0~127)
	constexpr MixedInputConfig& setJoystickThreshold(uint8_t threshold)
	{
		m_joystickThreshold = threshold;
		return *this;
	}

	// ── Getters ──
	const Source& xySource() const { return m_xySource; }
	bool xyInvertX() const { return m_xyInvertX; }
	bool xyInvertY() const { return m_xyInvertY; }

	const Source& rxrySource() const { return m_rxrySource; }
	bool rxryInvertX() const { return m_rxryInvertX; }
	bool rxryInvertY() const { return m_rxryInvertY; }

	const Source& dpadSource() const { return m_dpadSource; }
	bool dpadSwapAB() const { return m_dpadSwapAB; }
	bool dpadSwapXY() const { return m_dpadSwapXY; }
	bool dpadSwapABXY() const { return m_dpadSwapABXY; }

	const Source& buttonsSource() const { return m_buttonsSource; }
	bool buttonsSwapAB() const { return m_buttonsSwapAB; }
	bool buttonsSwapXY() const { return m_buttonsSwapXY; }
	bool buttonsSwapABXY() const { return m_buttonsSwapABXY; }

	uint8_t joystickThreshold() const { return m_joystickThreshold; }
	bool isValid() const { return m_valid; }
};

/// HID 输出状态（混合后的目标状态）
struct MixedHidState {
	int8_t x = 0, y = 0;
	int8_t rx = 0, ry = 0;
	uint8_t hat = 15;       // 0-7=方向, 15=松开 (HID hat switch)
	uint16_t buttons = 0;

	bool operator==(const MixedHidState& o) const;
	bool operator!=(const MixedHidState& o) const { return !(*this == o); }
};

/// 默认混合配置
///   gamepad(0,0) 摇杆 → X/Y
///   gamepad(0,1) 摇杆 → RX/RY
///   gamepad(0,0) ABXY → D-Pad
///   无 buttons 源
static constexpr MixedInputConfig kDefaultMixedConfig = []() constexpr {
	MixedInputConfig c;
	c.setXYSource({ 0, 0, SourceType::JOYSTICK })
		.setRXRYSource({ 0, 1, SourceType::JOYSTICK })
		.setDpadSource({ 0, 0, SourceType::BUTTONS }, true, true, false)
		.setButtonsSource({ 0, 1, SourceType::BUTTONS })  // 无效源
		.setJoystickThreshold(30);
	return c;
	}();

/// HID 报文大小（字节）
static constexpr size_t kHidReportSize = 7;

/// 从 VirtualGamepad 读取多源状态，按配置混合输出
MixedHidState mixInputs(const MixedInputConfig& config);

/// 将 MixedHidState 序列化为 HID 报文字节
void buildHidReport(const MixedHidState& state, uint8_t out[kHidReportSize]);
