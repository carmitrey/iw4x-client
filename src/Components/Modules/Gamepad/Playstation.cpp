#include "Playstation.hpp"

#include "Decode.hpp"

#include <algorithm>
#include <array>

namespace Components::GamepadControls
{
	namespace
	{
		// The calibration feature report, which both families answer over
		// Bluetooth under the same id and size (Linux
		// drivers/hid/hid-playstation.c: DS_FEATURE_REPORT_CALIBRATION and
		// DS4_FEATURE_REPORT_CALIBRATION_BT are both 0x05, both 41 bytes,
		// the DualShock 4's USB-only 0x02/37 variant is not needed here,
		// since USB never has to be switched out of minimal mode).
		constexpr uint8_t ps_feature_calibration{0x05};
		constexpr size_t ps_feature_calibration_size{41};

		// The report id both families use for the minimal Bluetooth
		// report. The extended reports the drivers decode are 0x31
		// (DualSense) and 0x11 (DualShock 4), so this id over Bluetooth is
		// unambiguously the minimal one.
		constexpr uint8_t ps_report_bt_minimal{0x01};

		// Upper bound on a feature report exchange, matching the
		// transport's own read buffer bound. The longest feature report
		// either family declares is well under this (the DualSense
		// firmware-info report, at 64 bytes, is the largest).
		constexpr size_t max_feature_size{128};
	}

	bool enable_extended_reports(hid_device& hid, device_id device) noexcept
	{
		// The platform sizes a feature exchange from the buffer rather
		// than from the report id in it, and refuses one shorter than the
		// collection's longest feature report. So the buffer is the
		// device's declared length, not the 41 bytes the calibration
		// report itself occupies.
		const size_t n(std::clamp(hid.feature_report_length(),
			ps_feature_calibration_size, max_feature_size));

		std::array<std::byte, max_feature_size> buf{};
		buf[0] = static_cast<std::byte>(ps_feature_calibration);

		if (!hid.get_feature(std::span<std::byte>(buf.data(), n)))
		{
			Logger::Warning(Game::CON_CHANNEL_SYSTEM,
				"gamepad device {}: unable to read the calibration feature report over "
				"Bluetooth; the controller may keep sending minimal reports and produce "
				"no input\n",
				device.value());
			return false;
		}

		return true;
	}

	bool minimal_bluetooth_report(std::span<const std::byte> r, connection link) noexcept
	{
		return link == connection::bluetooth &&
			!r.empty() &&
			rd_u8(r, 0) == ps_report_bt_minimal;
	}
}
