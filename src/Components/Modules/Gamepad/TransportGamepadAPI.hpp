#pragma once

#include "GamepadAPI.hpp"
#include "DeviceDiscovery.hpp"
#include "DeviceRegistry.hpp"
#include "DriverSet.hpp"
#include "Output.hpp"
#include "Sample.hpp"
#include "XInputModule.hpp"

#include <optional>

namespace Components::GamepadControls
{
	// GamepadAPI implementation backed by the non-blocking transport/
	// registry/driver stack (IW-2.2/2.3/2.4), replacing the old blocking
	// XInputGamePadAPI/DualSenseGamePadAPI backends (IW-2.8). This is the
	// actual root-cause fix this port exists for: Fetch() never blocks on
	// hardware I/O.
	//
	// driver::poll() returning false means "no new report since the last
	// poll", the normal case at engine frame rates well above a
	// controller's own report rate, not a disconnect. Fetch() therefore
	// keeps returning true and Read*() keeps serving the last good sample
	// as long as a device stays bound in the registry; only a device that
	// has actually left makes Fetch() report false, preserving
	// Controller::UpdateState's existing "false -> mark disabled"
	// contract and the "held deflection reads identically every frame"
	// invariant.
	class TransportGamepadAPI final : public GamepadAPI
	{
	public:
		TransportGamepadAPI();

		bool PlugIn(uint8_t portIndex) override;

		bool Fetch() override;
		void ReadSticks(Game::vec2_t& leftStick, Game::vec2_t& rightStick) override;
		void ReadDigitals(unsigned short& digitals) override;
		void ReadAnalogs(float& leftTrigger, float& rightTrigger) override;

		void UpdateRumbles(float left, float right) override;
		void UpdateLights(uint32_t color) override;
		void StopRumbles() override;
		void Send() override;

		// UpdateForceFeedback is deliberately left at GamepadAPI's no-op
		// default: the driver layer (IW-2.4) only encodes rumble/light
		// bar/player LEDs. Adaptive-trigger resistance has no encoder
		// until IW-2.10, so force feedback is inert through this backend
		// for now rather than silently pretending to honor it.

		// Notify discovery that the OS reported a device change, so the
		// next scan() runs immediately instead of waiting out the
		// interval. Forwarded from Window::OnDeviceChange.
		void NotifyDeviceChange() noexcept { discovery_.notify_device_change(); }

		// Reconcile against the registry now, rate-limited internally by
		// discovery (always scans on its first call). Safe and cheap to
		// call every frame, including while not yet bound -- this is
		// what lets a controller that raises no hotplug notification
		// (XInput) still be picked up within discovery::interval.
		bool TryBind();

		// The last sample's right-stick vector at the .normalized stage:
		// device-neutral and radial-clamped, but with no deadzone or
		// curve applied. Consumed by the aim pipeline (Gamepad.cpp's
		// AimAssist_ApplyTurnRates), which owns deadzone/curve shaping
		// itself so it happens exactly once for aim -- unlike
		// Controller::sticks[2]/[3], which stays deadzone-remapped by
		// Controller::ApplyDeadzone for every other existing consumer
		// (K_RSTICK_* generation, click-declick on deflection, etc).
		stick_vector RightStickRaw() const noexcept
		{
			return lastSample_.sticks[static_cast<size_t>(stick::right)].normalized;
		}

	private:
		void Reconcile();

		xinput_module xinput_;
		registry registry_;
		discovery discovery_;
		driver_set drivers_;

		device_id boundDevice_{};
		canonical_sample lastSample_{};

		float pendingLowRumble_{0.0f};
		float pendingHighRumble_{0.0f};
		std::optional<rumble_request> lastSentRumble_;

		light_bar_request pendingLight_{};
		std::optional<light_bar_request> lastSentLight_;
	};
}
