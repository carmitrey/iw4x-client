#pragma once

#include "CalibrationProfile.hpp"

#include <string>

namespace Components::GamepadControls
{
	// Validate a calibration profile.
	//
	// A profile is external data, loaded from a file or set by the user,
	// so it is validated before it is ever applied. The checks are that
	// the version is supported, every value is finite, stick ranges and
	// trigger travel are strictly positive, the drift threshold is a
	// fraction, and the motion scales and smoothing are non-negative. On
	// failure returns false and sets why to a precise reason; the caller
	// reports it and falls back to the default profile.
	bool validate(const profile&, std::string& why) noexcept;
}
