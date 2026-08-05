#pragma once

#include "DeviceIdentity.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Components::GamepadControls
{
	// Identity a HID device advertises about itself.
	struct hid_attributes
	{
		vendor_id                vendor;
		product_id               product;
		std::optional<uint16_t>  version;
	};

	// A HID endpoint a driver reads reports from and writes outputs to.
	//
	// This is an interface, not the Windows implementation: driver decoding
	// logic depends only on it, so it can be exercised against captured
	// report bytes with no operating-system I/O. The concrete device
	// (HidDevice.cpp) opens the OS handle and performs the reads and writes.
	//
	// link() names the report framing the device is presenting, which a
	// driver requires before decoding: USB and Bluetooth reports carry
	// different report ids, lengths, common-block offsets, and checksum
	// rules.
	class hid_device
	{
	public:
		virtual ~hid_device() = default;

		virtual connection link() const noexcept = 0;

		virtual hid_attributes attributes() const noexcept = 0;

		// The longest feature report this collection declares, which is the
		// size a get_feature() buffer must be: the platform sizes the transfer
		// from the buffer rather than from the report id, and rejects a
		// shorter one even when the report being asked for would fit. Zero
		// when the device declares no feature reports.
		virtual size_t feature_report_length() const noexcept = 0;

		// Read the next pending input report into buf without blocking.
		//
		// Returns the byte count, 0 when no report is pending, or nullopt on
		// error or disconnect. Must not throw.
		virtual std::optional<size_t> read(std::span<std::byte> buf) noexcept = 0;

		// Write one output report; buf[0] is the report id. Returns the byte
		// count written or nullopt on failure. Must not throw.
		virtual std::optional<size_t> write(std::span<const std::byte> buf) noexcept = 0;

		// Exchange a feature report (used by calibration and configuration).
		// buf[0] is the report id on entry, and buf must be at least
		// feature_report_length() bytes. Returns whether the exchange
		// succeeded.
		virtual bool get_feature(std::span<std::byte> buf) noexcept = 0;

		virtual bool set_feature(std::span<const std::byte> buf) noexcept = 0;
	};

	// A HID device interface the operating system currently reports as
	// present.
	//
	// The interface path is the stable identity: the same physical
	// attachment yields the same path across enumeration passes, and it is
	// what open() needs. Everything else here is what could be learned
	// without claiming the device.
	//
	// link is the report framing the device is presenting, named by its input
	// report length (see classify_link). It stays connection::unknown when
	// that length matches no framing we decode, and such a device is not
	// bound to a driver: a guess would mean decoding a report against the
	// wrong layout, and eventually emitting an output report against the
	// wrong one.
	struct hid_enumeration_entry
	{
		std::wstring   path;
		hid_attributes attributes;
		connection     link{connection::unknown};
		size_t         input_report_length{0};
	};

	// Name the report framing an input report length implies: 64 bytes is the
	// USB framing, 78 the Bluetooth one, and any other length is one we have
	// no layout for (connection::unknown).
	//
	// The length is taken from the parsed report descriptor and is therefore
	// available before the device is opened and stable regardless of the
	// mode the device is currently reporting in.
	connection classify_link(size_t input_report_length) noexcept;

	// Enumerate the present HID device interfaces that belong to a supported
	// controller family.
	//
	// Devices of other families, and non-gamepad HID collections such as
	// keyboards, are filtered out here rather than by the caller, so no
	// unrelated device is ever opened. Enumeration only queries; it never
	// writes to a device. Failures are logged and yield a shorter list rather
	// than an exception.
	std::vector<hid_enumeration_entry> enumerate();

	// Open the device at an interface path, or return nullptr on failure.
	//
	// The attributes, report length, and link are re-established from the
	// opened handle rather than carried over from enumeration: a device can
	// be unplugged between the two, and the report length in particular is
	// what every read is sized by. A device that is no longer a supported
	// controller, or whose link is ambiguous, is refused here as it is during
	// enumeration.
	//
	// A device that cannot be opened for writing is still opened read-only:
	// the drivers read reports on every frame and, as long as output reports
	// are withheld, never need the write handle. Failure is logged.
	std::unique_ptr<hid_device> open(const std::wstring& path);
}
