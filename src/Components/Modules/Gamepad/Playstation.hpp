#pragma once

#include "DeviceId.hpp"
#include "DeviceIdentity.hpp"
#include "HidDevice.hpp"

#include <span>

namespace Components::GamepadControls
{
	// Switch a PlayStation controller reached over Bluetooth into extended-
	// report mode.
	//
	// Over Bluetooth both families power up in a minimal mode, emitting a
	// 10-byte 0x01 report that carries only the sticks and the buttons, no
	// gyroscope, no accelerometer, no touchpad. They begin sending the full
	// report the drivers decode (DualSense 0x31, DualShock 4 0x11) only
	// once the host has read their calibration feature report. Reading it
	// is the whole point of this call: the calibration payload itself is
	// discarded, and it is the side effect on the device's reporting mode
	// that is wanted. This is the same nudge Linux
	// drivers/hid/hid-playstation.c relies on, where it falls out of
	// dualsense_get_calibration_data() being called during probe.
	//
	// Over USB the full report is sent from the start and this is
	// unnecessary.
	//
	// Returns whether the feature report was read. A false return is not
	// fatal and the caller should still bind: a pad that is already in
	// extended mode reports perfectly well without it.
	bool enable_extended_reports(hid_device&, device_id) noexcept;

	// Whether a report is the minimal-mode Bluetooth report described
	// above.
	//
	// Such a report is not malformed, it is a mode the device has not left
	// yet, so a driver drops it without reporting a decode failure.
	bool minimal_bluetooth_report(std::span<const std::byte>, connection link) noexcept;
}
