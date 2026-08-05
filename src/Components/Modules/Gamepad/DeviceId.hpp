#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <ostream>

namespace Components::GamepadControls
{
	// Stable subsystem handle for a discovered physical device.
	//
	// The registry assigns identifiers monotonically as devices arrive. A
	// value is never reused within a process, so a handle that outlives its
	// device can be recognized as stale rather than silently aliasing a
	// different one. The null value (0) denotes "no device" and is the only
	// value that compares unequal to every assigned handle.
	class device_id
	{
	public:
		constexpr device_id() = default;

		constexpr explicit device_id(uint32_t v) noexcept : value_(v) {}

		constexpr uint32_t value() const noexcept { return value_; }

		// A handle is valid iff it refers to an assigned device.
		constexpr explicit operator bool() const noexcept { return value_ != 0; }

		friend constexpr bool operator==(device_id, device_id) noexcept = default;
		friend constexpr std::strong_ordering operator<=>(device_id, device_id) noexcept = default;

	private:
		uint32_t value_{0};
	};

	inline constexpr device_id no_device{};

	std::ostream& operator<<(std::ostream&, device_id);

	// Operating-system transport that delivers a device's input.
	//
	// The transport decides how reports are acquired, not what the device
	// is. A single physical controller can be reachable over more than one
	// transport at once (for example a DualSense visible both as raw HID
	// and, once Steam virtualizes it, as an XInput pad); resolving that
	// ambiguity is discovery's job, not this enumeration.
	enum class transport_kind : uint8_t
	{
		unknown,
		xinput,     // XInput user index; capabilities only, no raw report access.
		raw_input,  // Windows Raw Input HID delivery (WM_INPUT).
		hid,        // Direct HID access (HidD_*/ReadFile/WriteFile).
	};

	const char* to_string(transport_kind) noexcept;
	std::ostream& operator<<(std::ostream&, transport_kind);

	// XInput user index in the closed range [0, count).
	//
	// XInput addresses controllers by a fixed set of four user slots. The
	// type is deliberately narrow: it can only hold a valid slot, so code
	// that has a user_index in hand never has to re-validate the port.
	class user_index
	{
	public:
		static constexpr uint8_t count{4};

		// Construct from a raw slot. The caller must have already
		// established that the slot is in range; see try_from for the
		// checked boundary conversion.
		constexpr explicit user_index(uint8_t v) noexcept : value_(v) {}

		static constexpr std::optional<user_index> try_from(int v) noexcept
		{
			if (v < 0 || v >= count)
				return std::nullopt;

			return user_index(static_cast<uint8_t>(v));
		}

		constexpr uint8_t value() const noexcept { return value_; }

		friend constexpr bool operator==(user_index, user_index) noexcept = default;
		friend constexpr std::strong_ordering operator<=>(user_index, user_index) noexcept = default;

	private:
		uint8_t value_;
	};
}
