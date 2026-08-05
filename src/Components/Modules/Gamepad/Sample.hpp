#pragma once

#include "Capability.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Components::GamepadControls
{
	// Which analog stick a reading belongs to.
	enum class stick : uint8_t
	{
		left,
		right,
	};

	inline constexpr size_t stick_count{2};

	const char* to_string(stick) noexcept;

	// A stick position in normalized space.
	//
	// Both components lie in [-1, 1] after normalization; the magnitude may
	// exceed 1 only transiently before radial clamping. This is device-neutral
	// space: it says nothing about screen or world orientation, which the aim
	// layer introduces with its own units.
	struct stick_vector
	{
		float x{0.0f};
		float y{0.0f};

		// Euclidean magnitude sqrt(x^2 + y^2).
		float magnitude() const noexcept;
	};

	// Device-raw stick reading.
	//
	// The units are whatever the device report carries (for XInput, a signed
	// 16-bit count per axis). Preserved unmodified so calibration and
	// diagnostics can work from exactly what the hardware sent.
	struct stick_raw
	{
		int32_t x{0};
		int32_t y{0};
	};

	// The four processing stages a stick carries through the pipeline.
	//
	// Keeping every stage lets calibration recompute from raw and lets debug
	// tooling show precisely where a value changed. The game path consumes
	// filtered; everything before it is retained rather than discarded.
	struct stick_sample
	{
		stick_raw    raw{};         // Exactly as decoded from the device report.
		stick_vector normalized{};  // Device-neutral, radial-clamped to [-1, 1].
		stick_vector calibrated{};  // Per-device center and range correction applied.
		stick_vector filtered{};    // Deadzone, curves, and smoothing applied; the
		                            // stage the game path reads. The aim layer
		                            // owns deadzone and curves; the driver
		                            // fills only raw and normalized.
	};

	// Which analog trigger a reading belongs to.
	enum class trigger_side : uint8_t
	{
		left,
		right,
	};

	inline constexpr size_t trigger_count{2};

	const char* to_string(trigger_side) noexcept;

	// One analog trigger reading.
	//
	// raw preserves the device units (XInput and the PlayStation families both
	// report 0..255) for calibration and diagnostics; normalized is the
	// device-neutral [0, 1] value the game path consumes after the trigger
	// deadzone is applied. The digital "pressed" decision is a mapping concern
	// and is not baked in here.
	struct trigger_sample
	{
		uint16_t raw{0};
		float    normalized{0.0f};
	};

	// One touchpad contact.
	//
	// Coordinates are in the device's own touch resolution (for the DualShock
	// 4 and DualSense this is 1920 x 943), not a normalized or screen space; a
	// consumer that wants screen space converts explicitly. id is the
	// hardware's contact tracking identifier, which increments as new touches
	// begin and lets a consumer follow one finger across frames. When active
	// is false the remaining fields are stale and must not be read.
	struct touch_point
	{
		bool     active{false};
		uint8_t  id{0};
		uint16_t x{0};
		uint16_t y{0};
	};

	// Touchpad state for one sample.
	//
	// The DualShock 4 and DualSense report up to two simultaneous contacts. A
	// device without a touchpad publishes no touchpad state at all (the
	// canonical sample's optional is left empty) rather than an all-inactive
	// one, so that "no touchpad" and "touchpad not touched" stay
	// distinguishable.
	struct touchpad
	{
		static constexpr size_t max_points{2};

		std::array<touch_point, max_points> points{};
	};

	// A three-component vector in the controller's own sensor frame.
	//
	// Deliberately its own type: sensor-frame vectors must not be assigned to
	// or from the screen-space and world-space vectors the aim layer uses. The
	// axes are the controller's physical axes, not the game's.
	struct sensor_vec3
	{
		float x{0.0f};
		float y{0.0f};
		float z{0.0f};
	};

	// Gyroscope reading: angular velocity in radians per second.
	//
	// The unit is fixed here so every driver converts its device-specific raw
	// counts into the same physical quantity; a consumer never has to know
	// which family produced the sample.
	struct gyro_sample
	{
		sensor_vec3 angular_velocity{};
	};

	// Accelerometer reading: proper acceleration in standard gravities
	// (1 g = 9.80665 m/s^2).
	struct accel_sample
	{
		sensor_vec3 acceleration{};
	};

	// Combined motion reading for one sample.
	//
	// device_timestamp, when present, is the controller's own sensor clock as
	// reported in the HID payload. It is retained for consumers that
	// integrate motion over the device's timeline; it is not the subsystem
	// clock and is never used for latency.
	struct motion_sample
	{
		gyro_sample             gyro{};
		accel_sample            accel{};
		std::optional<uint32_t> device_timestamp;
	};

	// Battery state at the time of a sample.
	struct battery_state
	{
		enum class status : uint8_t
		{
			unknown,
			discharging,
			charging,
			full,
		};

		status                 state{status::unknown};
		std::optional<uint8_t> percent;  // 0..100 when the device reports a level.
	};

	const char* to_string(battery_state::status) noexcept;

	// Canonical physical button across the supported families.
	//
	// This is the device-neutral physical set, the thing under the player's
	// finger, named by position rather than by any single vendor's label. The
	// translation from a physical button to a logical game action belongs to
	// a future mapping layer and never happens here. Positional names
	// (face_south and so on) are used precisely so this layer stays
	// independent of the Xbox/PlayStation labelling the glyph layer later
	// re-applies.
	//
	// Trigger fully-pressed states (l2/r2) are present as buttons so that
	// edge detection and digital binds work uniformly; the analog value lives
	// in the trigger sample.
	enum class button : uint8_t
	{
		face_south,     // Xbox A     / Cross.
		face_east,      // Xbox B     / Circle.
		face_west,      // Xbox X     / Square.
		face_north,     // Xbox Y     / Triangle.

		dpad_up,
		dpad_down,
		dpad_left,
		dpad_right,

		l1,             // Left shoulder.
		r1,             // Right shoulder.
		l2,             // Left trigger, fully pressed.
		r2,             // Right trigger, fully pressed.
		l3,             // Left stick click.
		r3,             // Right stick click.

		start,          // Xbox Menu  / Options.
		back,           // Xbox View  / Create/Share.
		guide,          // Xbox/Guide / PS.

		touchpad,       // Touchpad click (DualShock 4 / DualSense).
		mute,           // Microphone mute (DualSense).

		edge_paddle_left,   // DualSense Edge rear left paddle.
		edge_paddle_right,  // DualSense Edge rear right paddle.
		edge_fn_left,       // DualSense Edge left function button (Fn1).
		edge_fn_right,      // DualSense Edge right function button (Fn2).

		count,
	};

	static_assert(static_cast<size_t>(button::count) <= 32,
		"button_set stores buttons in a 32-bit mask");

	const char* to_string(button) noexcept;

	// Set of physical buttons held in one sample.
	//
	// A fixed 32-bit mask: allocation-free and trivially copyable so it costs
	// nothing on the per-frame path. Edge detection is expressed against a
	// previous set rather than stored per button, which keeps the sample
	// itself stateless.
	class button_set
	{
	public:
		constexpr button_set() = default;

		constexpr bool down(button b) const noexcept
		{
			return (bits_ & mask(b)) != 0;
		}

		constexpr void set(button b, bool on) noexcept
		{
			if (on)
				bits_ |= mask(b);
			else
				bits_ &= ~mask(b);
		}

		constexpr bool any() const noexcept { return bits_ != 0; }

		constexpr uint32_t value() const noexcept { return bits_; }

		// Buttons that are down in this set but were up in prev.
		constexpr button_set pressed_since(button_set prev) const noexcept
		{
			return from_bits(bits_ & ~prev.bits_);
		}

		// Buttons that are up in this set but were down in prev.
		constexpr button_set released_since(button_set prev) const noexcept
		{
			return from_bits(prev.bits_ & ~bits_);
		}

		friend constexpr bool operator==(button_set, button_set) noexcept = default;

	private:
		static constexpr uint32_t mask(button b) noexcept
		{
			return uint32_t{1} << static_cast<uint32_t>(b);
		}

		static constexpr button_set from_bits(uint32_t b) noexcept
		{
			button_set s;
			s.bits_ = b;
			return s;
		}

		uint32_t bits_{0};
	};

	// Minimally-processed, device-specific reading.
	//
	// A raw sample is what the driver decoded straight from the report,
	// before any device-neutral interpretation. It exists so calibration can
	// measure a real device and so diagnostics can show exactly what arrived;
	// the game path never reads it. Its fields carry device-native units and
	// bit layouts, so it is only meaningful together with the device's
	// family.
	struct raw_sample
	{
		std::array<stick_raw, stick_count>  sticks{};
		std::array<uint16_t, trigger_count> triggers{};
		uint32_t                            buttons{0};  // Device-native button bits.
		std::optional<motion_sample>        motion;
		std::optional<touchpad>             touch;
		std::optional<battery_state>        battery;
	};

	// Device-neutral, game-facing input state.
	//
	// The canonical sample is loss-aware: an absent optional means "this
	// device does not report this," which is a different fact from a
	// present-but-zero value. A DualSense fills touch and motion; an XInput
	// pad leaves them empty; neither is coerced into the other's shape. caps
	// records what the producing device can actually do, so a consumer can
	// present or withhold a feature without consulting the driver.
	struct canonical_sample
	{
		button_set                                buttons{};
		std::array<stick_sample, stick_count>     sticks{};
		std::array<trigger_sample, trigger_count> triggers{};
		std::optional<touchpad>                   touch;
		std::optional<motion_sample>              motion;
		std::optional<battery_state>              battery;
		capabilities                              caps{};
	};
}
