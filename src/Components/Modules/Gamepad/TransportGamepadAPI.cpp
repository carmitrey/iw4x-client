#include "TransportGamepadAPI.hpp"

#include "Controller.hpp"

namespace Components::GamepadControls
{
	namespace
	{
		bool same_rumble(const rumble_request& a, const rumble_request& b) noexcept
		{
			return a.low_frequency == b.low_frequency && a.high_frequency == b.high_frequency;
		}

		bool same_light(const light_bar_request& a, const light_bar_request& b) noexcept
		{
			return a.red == b.red && a.green == b.green && a.blue == b.blue;
		}

		// gpad_force_xinput_only's contract (IW-2.8): the old two-backend
		// PlugIn enforced it by only ever trying XInputGamePadAPI, never
		// DualSenseGamePadAPI. Here, where one registry can hold devices
		// of any bound family, the equivalent is skipping non-XInput
		// drivers when selecting which bound device to poll/report.
		bool acceptable(const driver& drv) noexcept
		{
			return !Controller::GetForceXInputOnly() || drv.family() == family::xbox;
		}
	}

	TransportGamepadAPI::TransportGamepadAPI()
		: discovery_(registry_, xinput_)
		, drivers_(xinput_)
	{
	}

	void TransportGamepadAPI::Reconcile()
	{
		// gpad_force_xinput_only means only XInput-family devices are ever
		// wanted, so skip HID enumeration entirely rather than just
		// throttling it (see discovery::hid_interval's comment) -- checked
		// every call since the dvar can change at runtime.
		discovery_.set_hid_enumeration_enabled(!Controller::GetForceXInputOnly());

		discovery_.scan();
		drivers_.reconcile(registry_);
	}

	bool TransportGamepadAPI::TryBind()
	{
		Reconcile();

		bool found(false);
		drivers_.for_each([&found](driver& drv, const device_connection&)
		{
			if (!found && acceptable(drv))
				found = true;
		});

		return found;
	}

	bool TransportGamepadAPI::PlugIn(uint8_t /*portIndex*/)
	{
		return TryBind();
	}

	bool TransportGamepadAPI::Fetch()
	{
		Reconcile();

		bool bound(false);
		drivers_.for_each([this, &bound](driver& drv, const device_connection& dev)
		{
			if (bound || !acceptable(drv))
				return;

			bound = true;
			boundDevice_ = dev.id;

			raw_sample raw{};
			canonical_sample fresh{};
			if (drv.poll(raw, fresh))
			{
				// A fresh report arrived; adopt it.
				lastSample_ = fresh;
			}
			// else: no new report since the last poll -- keep serving
			// lastSample_ so a held deflection reads identically every
			// frame, per the frame-invariant AC.
		});

		if (!bound)
		{
			boundDevice_ = no_device;
			lastSample_ = canonical_sample{};
		}

		return bound;
	}

	void TransportGamepadAPI::ReadSticks(Game::vec2_t& leftStick, Game::vec2_t& rightStick)
	{
		const auto& l(lastSample_.sticks[static_cast<size_t>(stick::left)].normalized);
		const auto& r(lastSample_.sticks[static_cast<size_t>(stick::right)].normalized);

		leftStick[0] = l.x;
		leftStick[1] = l.y;
		rightStick[0] = r.x;
		rightStick[1] = r.y;
	}

	void TransportGamepadAPI::ReadDigitals(unsigned short& digitals)
	{
		digitals = 0;

		const auto& b(lastSample_.buttons);

#define IW4X_TRANSLATE(physical, xinputBit) \
		if (b.down(physical)) \
			digitals |= (xinputBit)

		IW4X_TRANSLATE(button::dpad_up, XINPUT_GAMEPAD_DPAD_UP);
		IW4X_TRANSLATE(button::dpad_down, XINPUT_GAMEPAD_DPAD_DOWN);
		IW4X_TRANSLATE(button::dpad_left, XINPUT_GAMEPAD_DPAD_LEFT);
		IW4X_TRANSLATE(button::dpad_right, XINPUT_GAMEPAD_DPAD_RIGHT);
		IW4X_TRANSLATE(button::start, XINPUT_GAMEPAD_START);
		IW4X_TRANSLATE(button::back, XINPUT_GAMEPAD_BACK);
		IW4X_TRANSLATE(button::l3, XINPUT_GAMEPAD_LEFT_THUMB);
		IW4X_TRANSLATE(button::r3, XINPUT_GAMEPAD_RIGHT_THUMB);
		IW4X_TRANSLATE(button::l1, XINPUT_GAMEPAD_LEFT_SHOULDER);
		IW4X_TRANSLATE(button::r1, XINPUT_GAMEPAD_RIGHT_SHOULDER);
		IW4X_TRANSLATE(button::face_south, XINPUT_GAMEPAD_A);
		IW4X_TRANSLATE(button::face_east, XINPUT_GAMEPAD_B);
		IW4X_TRANSLATE(button::face_west, XINPUT_GAMEPAD_X);
		IW4X_TRANSLATE(button::face_north, XINPUT_GAMEPAD_Y);

#undef IW4X_TRANSLATE
	}

	void TransportGamepadAPI::ReadAnalogs(float& leftTrigger, float& rightTrigger)
	{
		leftTrigger = lastSample_.triggers[static_cast<size_t>(trigger_side::left)].normalized;
		rightTrigger = lastSample_.triggers[static_cast<size_t>(trigger_side::right)].normalized;
	}

	void TransportGamepadAPI::UpdateRumbles(float left, float right)
	{
		pendingLowRumble_ = left;
		pendingHighRumble_ = right;
	}

	void TransportGamepadAPI::StopRumbles()
	{
		pendingLowRumble_ = 0.0f;
		pendingHighRumble_ = 0.0f;
	}

	void TransportGamepadAPI::UpdateLights(uint32_t color)
	{
		pendingLight_.red = static_cast<uint8_t>(color & 0xFF);
		pendingLight_.green = static_cast<uint8_t>((color >> 8) & 0xFF);
		pendingLight_.blue = static_cast<uint8_t>((color >> 16) & 0xFF);
	}

	void TransportGamepadAPI::Send()
	{
		if (!boundDevice_)
			return;

		// Only submit an output request when the value actually changed
		// since the last one sent, matching the old DualSense backend's
		// dirty-tracking discipline (Controller::PushUpdates calls
		// UpdateLights with the same constant color every frame; without
		// this check that would resubmit a HID output report every
		// frame instead of once).
		const rumble_request rumble{pendingLowRumble_, pendingHighRumble_};
		if (!lastSentRumble_ || !same_rumble(*lastSentRumble_, rumble))
		{
			drivers_.submit(boundDevice_, rumble);
			lastSentRumble_ = rumble;
		}

		if (!lastSentLight_ || !same_light(*lastSentLight_, pendingLight_))
		{
			drivers_.submit(boundDevice_, pendingLight_);
			lastSentLight_ = pendingLight_;
		}
	}
}
