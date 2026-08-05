#pragma once

#include "Driver.hpp"
#include "HidDevice.hpp"

#include <span>

namespace Components::GamepadControls
{
	// Decode a DualShock 4 input report into raw and canonical samples.
	//
	// Handles both framings, USB report 0x01 (64 bytes) and Bluetooth
	// report 0x11 (78 bytes), selected by link. The report id, length,
	// and, for Bluetooth, the trailing CRC-32 are validated against the
	// incoming bytes before any field is read; on any mismatch the
	// function returns false and leaves the outputs untouched, because a
	// report that does not match the decoded framing must never be
	// associated with this device's layout. The function is pure and
	// performs no I/O, so it is exercised directly by captured fixtures.
	//
	// Field offsets follow drivers/hid/hid-playstation.c (struct
	// dualshock4_input_report_common and its USB/BT wrappers).
	bool decode_dualshock4(std::span<const std::byte> report, connection link, raw_sample&, canonical_sample&) noexcept;

	// First-class DualShock 4 driver.
	//
	// Owns the decode and output for one DualShock 4 over one HID
	// transport. It reads native HID reports and decodes the device's own
	// model, touchpad, motion, battery, connection-specific framing,
	// rather than forcing the device into an Xbox-shaped state. The
	// connection (USB or Bluetooth) is taken from the HID device and
	// never assumed.
	class dualshock4_driver : public driver
	{
	public:
		dualshock4_driver(hid_device&, device_id);

		GamepadControls::family family() const noexcept override { return GamepadControls::family::dualshock4; }
		device_id device() const noexcept override { return device_; }

		bool poll(raw_sample&, canonical_sample&) noexcept override;
		void submit(const output_request&) noexcept override;

	private:
		hid_device& hid_;
		device_id device_;
		connection link_;

		// Whether the device has been seen still sending minimal
		// Bluetooth reports. Reported once and then remembered: the
		// device sends them at its full report rate, so a diagnostic per
		// report would bury the log.
		bool minimal_reported_{false};
	};
}
