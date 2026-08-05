#pragma once

#include "DualSenseDriver.hpp"

#include <span>

namespace Components::GamepadControls
{
	// Decode a DualSense Edge input report.
	//
	// The Edge shares struct dualsense_input_report with the standard
	// DualSense, so this delegates to decode_dualsense with the Edge flag
	// set: the shared state is decoded once, and the four rear buttons
	// (two paddles, two function buttons) and the back-buttons capability
	// are added rather than discarded to fit the standard DualSense. The
	// caller has already identified the device as an Edge by its product
	// id before decoding.
	bool decode_dualsense_edge(std::span<const std::byte> report, connection link, raw_sample&, canonical_sample&) noexcept;

	// First-class DualSense Edge driver.
	//
	// Reuses the DualSense driver's HID read and shared decode, and
	// reports the Edge family so the mapping and glyph layers can present
	// Edge-specific affordances. Trigger-stop behavior, replaceable-
	// stick-module identity, and stored profile state are exposed as they
	// become decodable from verified feature reports; nothing
	// Edge-specific that the input report carries is dropped here.
	class dualsense_edge_driver : public dualsense_driver
	{
	public:
		using dualsense_driver::dualsense_driver;

		GamepadControls::family family() const noexcept override { return GamepadControls::family::dualsense_edge; }

		bool poll(raw_sample&, canonical_sample&) noexcept override;
	};
}
