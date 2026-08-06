#include "DeviceDiscovery.hpp"

#include "HidDevice.hpp"

#include <algorithm>
#include <cassert>
#include <variant>

namespace Components::GamepadControls
{
	namespace
	{
		constexpr capabilities dualsense_capabilities{
			capability::gyroscope |
			capability::accelerometer |
			capability::touchpad |
			capability::battery |
			capability::microphone_button |
			capability::rumble |
			capability::haptics |
			capability::adaptive_triggers |
			capability::light_bar |
			capability::player_leds};

		// What a family's device can do, as a fact about the hardware
		// rather than about what this subsystem currently drives. Output
		// capabilities are reported here even where the driver withholds
		// the corresponding output report, so a debug view and a future
		// encoder agree on the device.
		capabilities capabilities_for(family f) noexcept
		{
			switch (f)
			{
			case family::xbox:
				return capabilities(capability::rumble);

			case family::dualshock4:
				return capability::gyroscope |
					capability::accelerometer |
					capability::touchpad |
					capability::battery |
					capability::rumble |
					capability::light_bar;

			case family::dualsense:
				return dualsense_capabilities;

			case family::dualsense_edge:
				return dualsense_capabilities | capability::back_buttons;

			case family::unknown:
				break;
			}

			return capabilities();
		}
	}

	discovery::discovery(registry& r, const xinput_module& x)
		: registry_(r), xinput_(x)
	{
	}

	void discovery::scan()
	{
		const bool changed(pending_.exchange(false));
		const auto now(std::chrono::steady_clock::now());

		// A device-change notification forces a full scan now (matching
		// scan_now()); otherwise this is the periodic path, which
		// re-checks XInput every `interval` but HID far less often -- see
		// hid_interval's comment.
		if (!changed && scanned_ && now - last_scan_ < interval)
			return;

		if (changed || !scanned_)
		{
			last_scan_ = now;
			last_hid_scan_ = now;
			scanned_ = true;
			scan_now();
			return;
		}

		last_scan_ = now;

		std::vector<transport_binding> seen;
		seen.reserve(user_index::count + 4);

		scan_xinput(seen);

		if (hid_enabled_ && now - last_hid_scan_ >= hid_interval)
		{
			last_hid_scan_ = now;
			scan_hid(seen);
		}
		else if (hid_enabled_)
		{
			// Not re-enumerating HID this cycle: keep every already-bound
			// HID device's binding in `seen` so retire_unseen() does not
			// drop it just because this particular pass did not look.
			registry_.for_each([&seen](const device_connection& d)
			{
				if (std::holds_alternative<hid_binding>(d.binding))
					seen.push_back(d.binding);
			});
		}

		retire_unseen(seen);
	}

	void discovery::scan_now()
	{
		// Bindings observed in this pass. Sized for the four XInput slots
		// plus a handful of HID devices; growth beyond that is a reserve,
		// not a bug.
		std::vector<transport_binding> seen;
		seen.reserve(user_index::count + 4);

		scan_xinput(seen);

		if (hid_enabled_)
			scan_hid(seen);

		retire_unseen(seen);
	}

	void discovery::scan_xinput(std::vector<transport_binding>& seen)
	{
		if (!xinput_.loaded())
			return;

		for (uint8_t i(0); i < user_index::count; ++i)
		{
			XINPUT_CAPABILITIES caps{};

			// XInputGetCapabilities, unlike XInputGetState, is the
			// documented way to ask whether a slot holds a device: it
			// neither latches a packet number nor is affected by the state
			// cache. XINPUT_FLAG_GAMEPAD restricts the answer to game pads,
			// so a wheel or an arcade stick does not present itself as a
			// controller we know how to map.
			if (xinput_.get_capabilities(i, XINPUT_FLAG_GAMEPAD, caps) != ERROR_SUCCESS)
				continue;

			const user_index slot(i);

			// XInput does not report the physical link, and no XInput
			// decode depends on one: the state model is the same over USB
			// and over the wireless adaptor. The link is left unknown
			// rather than guessed, which is meaningful only for the HID
			// drivers, whose report framing differs.
			registry_.add(device_identity{family::xbox, std::nullopt, std::nullopt, std::nullopt},
				transport_kind::xinput,
				connection::unknown,
				capabilities_for(family::xbox),
				xinput_binding{slot});

			seen.push_back(xinput_binding{slot});
		}
	}

	void discovery::scan_hid(std::vector<transport_binding>& seen)
	{
		for (hid_enumeration_entry& e : enumerate())
		{
			const family f(classify(e.attributes.vendor, e.attributes.product));

			// enumerate() filters to families we drive and to devices whose
			// link it could establish; both are preconditions for binding
			// a driver.
			assert(f != family::unknown);
			assert(e.link != connection::unknown);

			registry_.add(device_identity{f,
					e.attributes.vendor,
					e.attributes.product,
					e.attributes.version},
				transport_kind::hid,
				e.link,
				capabilities_for(f),
				hid_binding{e.path});

			seen.push_back(hid_binding{std::move(e.path)});
		}
	}

	void discovery::retire_unseen(const std::vector<transport_binding>& seen)
	{
		// Collect first, remove second: for_each holds the registry lock,
		// and remove() takes it.
		std::vector<device_id> departed;

		registry_.for_each([&seen, &departed](const device_connection& d)
			{
				const bool present(
					std::any_of(seen.begin(), seen.end(),
						[&d](const transport_binding& b) { return same_binding(d.binding, b); }));

				if (!present)
					departed.push_back(d.id);
			});

		for (device_id id : departed)
			registry_.remove(id);
	}
}
