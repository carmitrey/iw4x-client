#pragma once

#include "DeviceIdentity.hpp"
#include "Sample.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace Components::GamepadControls
{
	// Where a calibration value came from.
	//
	// Recorded so a value's authority is never guessed: a built-in default,
	// a locally measured profile, and an explicit user setting are all
	// distinguishable.
	enum class value_source : uint8_t
	{
		built_in,  // The family's compiled-in default.
		measured,  // Derived from measuring this physical device.
		user,      // Set explicitly by the user.
	};

	const char* to_string(value_source) noexcept;

	// Per-stick centre and range correction, in the driver's normalized
	// space.
	//
	// Correction is applied to the driver's device-neutral normalized
	// output (up and right positive, nominally in [-1, 1]), not to raw
	// device units, so the profile stays free of family-specific sign and
	// scale conventions and the default is the identity (centre 0, range
	// 1). A stick's resting position drifts slightly from centre and its
	// travel rarely reaches unity; centre and range correct both so the
	// calibrated output is symmetric and reaches unity.
	struct stick_calibration
	{
		float center_x{0.0f};
		float center_y{0.0f};
		float range_x{1.0f};
		float range_y{1.0f};

		// Calibrated magnitude below this is treated as rest, absorbing
		// residual drift the centre correction does not.
		float drift_threshold{0.0f};
	};

	// Per-trigger travel, in the driver's normalized [0, 1] space.
	//
	// Defaults to the identity (0 to 1); a measured profile narrows it to
	// the trigger's real resting and fully-pressed values.
	struct trigger_calibration
	{
		float min{0.0f};
		float max{1.0f};
	};

	// Motion-sensor bias and scale.
	//
	// The bias is the raw reading at rest, subtracted before scaling; the
	// scale converts the debiased raw counts into the canonical physical
	// units (radians per second for the gyroscope, standard gravities for
	// the accelerometer). Without these a driver can only publish raw
	// counts.
	struct motion_calibration
	{
		sensor_vec3 gyro_bias{};
		sensor_vec3 accel_bias{};
		float gyro_scale{1.0f};   // rad/s per raw count.
		float accel_scale{1.0f};  // g per raw count.
	};

	// A complete, versioned per-device calibration profile.
	//
	// The version guards stored profiles against format changes; an
	// unsupported version is rejected rather than misread. family and the
	// optional device_key bind the profile to the kind of device, and,
	// when a stable device identifier is available, to the specific
	// device, so calibration is never silently shared across unrelated
	// hardware.
	//
	// device_key derivation is not implemented by this port: no scheme
	// for it exists anywhere in the reference source available here
	// (device_id is a process-lifetime handle, not a persistent one), so
	// device_key is always nullopt for now and calibration stays scoped
	// per-family.
	struct profile
	{
		static constexpr uint16_t current_version{1};

		uint16_t version{current_version};
		GamepadControls::family family{family::unknown};
		std::optional<uint64_t> device_key;
		value_source source{value_source::built_in};

		std::array<stick_calibration, stick_count> sticks{};
		std::array<trigger_calibration, trigger_count> triggers{};
		motion_calibration motion{};

		// Optional output-smoothing time constant in seconds; zero
		// disables it.
		float smoothing{0.0f};
	};

	// The compiled-in default profile for a family.
	//
	// Encodes the family's nominal stick centre and range and trigger
	// travel, so a device works before it is measured. Marked built_in.
	profile default_profile(GamepadControls::family) noexcept;
}
