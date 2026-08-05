#pragma once

#include <cstdint>
#include <optional>
#include <ostream>

namespace Components::GamepadControls
{
	// Report framing a device is presenting.
	//
	// USB and Bluetooth carry different report identifiers, lengths,
	// common-block offsets, and checksum rules, and this is what selects
	// between those layouts. A driver must never decode a report, and must
	// never emit an output report, until it is known unambiguously.
	//
	// Despite the name, nothing asks it which physical bus a device sits on,
	// and the two questions come apart: a wireless adapter is reached over USB
	// while the pad behind it speaks the Bluetooth framing. Reading `usb`
	// here as "attached by a USB cable" is a mistake.
	enum class connection : uint8_t
	{
		unknown,
		usb,
		bluetooth,
		virtualized,  // Presented by software (e.g. a Steam-created XInput pad).
	};

	const char* to_string(connection) noexcept;
	std::ostream& operator<<(std::ostream&, connection);

	// USB vendor identifier.
	class vendor_id
	{
	public:
		constexpr explicit vendor_id(uint16_t v) noexcept : value_(v) {}

		constexpr uint16_t value() const noexcept { return value_; }

		friend constexpr bool operator==(vendor_id, vendor_id) noexcept = default;

	private:
		uint16_t value_;
	};

	// USB product identifier.
	class product_id
	{
	public:
		constexpr explicit product_id(uint16_t v) noexcept : value_(v) {}

		constexpr uint16_t value() const noexcept { return value_; }

		friend constexpr bool operator==(product_id, product_id) noexcept = default;

	private:
		uint16_t value_;
	};

	// Controller family the subsystem drives natively.
	//
	// The family is the decoding contract: it selects which driver owns the
	// device and which native report model applies. It is intentionally not a
	// marketing label. "xbox" means "speaks the XInput state model"; the
	// three PlayStation families each decode a distinct HID report set and
	// must not be collapsed into one another (a DualSense Edge carries state
	// a DualSense decoder cannot represent).
	enum class family : uint8_t
	{
		unknown,
		xbox,            // XInput-class controller.
		dualshock4,      // Sony DualShock 4 (CUH-ZCT1/ZCT2).
		dualsense,       // Sony DualSense (CFI-ZCT1).
		dualsense_edge,  // Sony DualSense Edge (CFI-ZER1).
	};

	const char* to_string(family) noexcept;
	std::ostream& operator<<(std::ostream&, family);

	// Well-known USB identifiers.
	//
	// Sourced from the Linux kernel HID driver drivers/hid/hid-playstation.c
	// and drivers/hid/hid-ids.h, which the PlayStation drivers in this
	// subsystem are designed against. Keep these next to the classifier that
	// consumes them so a maintainer can check an id against the kernel
	// without leaving the file.
	inline constexpr vendor_id vendor_sony{0x054C};
	inline constexpr vendor_id vendor_microsoft{0x045E};

	inline constexpr product_id product_ds4_gen1{0x05C4};        // CUH-ZCT1 controller.
	inline constexpr product_id product_ds4_gen2{0x09CC};        // CUH-ZCT2 controller.
	inline constexpr product_id product_ds4_dongle{0x0BA0};      // USB wireless adaptor.
	inline constexpr product_id product_dualsense{0x0CE6};       // CFI-ZCT1 controller.
	inline constexpr product_id product_dualsense_edge{0x0DF2};  // CFI-ZER1.

	// Classify a HID device by its USB identifiers.
	//
	// Returns family::unknown for anything the subsystem has no native driver
	// for. A caller must treat unknown as "do not decode": guessing a report
	// layout from an unrecognized device is exactly the ambiguity the driver
	// layer refuses to act on.
	family classify(vendor_id, product_id) noexcept;

	// What a device is, independent of how it is currently connected.
	//
	// Connection and transport live on the device's connection record, not
	// here: the same identity can be reached over USB or Bluetooth without
	// becoming a different device.
	struct device_identity
	{
		GamepadControls::family family{family::unknown};
		std::optional<vendor_id>  vendor;
		std::optional<product_id> product;

		// Device release in binary-coded decimal from the USB descriptor,
		// when the transport exposes it. Used only to disambiguate hardware
		// revisions; it is never required for correct decoding.
		std::optional<uint16_t> release;
	};
}
