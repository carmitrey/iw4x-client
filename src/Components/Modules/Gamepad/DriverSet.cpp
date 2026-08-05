#include "DriverSet.hpp"

#include "DualSenseDriver.hpp"
#include "DualSenseEdgeDriver.hpp"
#include "DualShock4Driver.hpp"
#include "Playstation.hpp"
#include "XInputDriver.hpp"

#include <algorithm>
#include <cassert>
#include <variant>

namespace Components::GamepadControls
{
	driver_set::driver_set(const xinput_module& x)
		: xinput_(x)
	{
	}

	std::unique_ptr<driver> driver_set::bind(const device_connection& d, std::unique_ptr<hid_device>& hid)
	{
		switch (d.identity.family)
		{
		case family::xbox:
		{
			const auto* b(std::get_if<xinput_binding>(&d.binding));

			if (b == nullptr)
				break;

			return std::make_unique<xinput_driver>(xinput_, d.id, b->index);
		}

		case family::dualshock4:
		case family::dualsense:
		case family::dualsense_edge:
		{
			if (hid == nullptr)
				break;

			// The link is what selects the report framing, and the
			// device layer never records a HID device whose link it
			// could not establish.
			assert(hid->link() == connection::usb || hid->link() == connection::bluetooth);

			// Over Bluetooth these pads power up sending a minimal
			// report the drivers have no layout for, and only start
			// sending the extended one once their calibration feature
			// report has been read. Do that before the driver is handed
			// the device, so the first poll already has something to
			// decode. Binding does not depend on it: a pad already in
			// extended mode needs no nudge, and one that refuses the
			// exchange is still worth binding.
			if (hid->link() == connection::bluetooth)
				enable_extended_reports(*hid, d.id);

			switch (d.identity.family)
			{
			case family::dualshock4:
				return std::make_unique<dualshock4_driver>(*hid, d.id);

			case family::dualsense:
				return std::make_unique<dualsense_driver>(*hid, d.id);

			case family::dualsense_edge:
				return std::make_unique<dualsense_edge_driver>(*hid, d.id);

			default:
				break;
			}

			break;
		}

		case family::unknown:
			break;
		}

		return nullptr;
	}

	void driver_set::reconcile(const registry& r)
	{
		const uint64_t g(r.generation());

		if (reconciled_ && g == generation_)
			return;

		generation_ = g;
		reconciled_ = true;

		// Copy the current membership out from under the lock before
		// doing anything that could take it again, or that could be slow
		// (opening a HID handle).
		std::vector<device_connection> current;
		r.for_each([&current](const device_connection& d) { current.push_back(d); });

		// Retire drivers whose device is gone. The transport handle is
		// released with the driver that borrowed it.
		std::erase_if(entries_, [&current](const entry& e)
		{
			const bool gone(
				std::none_of(current.begin(), current.end(),
					[&e](const device_connection& d) { return d.id == e.device.id; }));

			if (gone)
				Logger::Print(Game::CON_CHANNEL_SYSTEM, "gamepad driver released: {}\n", to_string(e.device.identity.family));

			return gone;
		});

		// Bind drivers for devices that have appeared.
		for (device_connection& d : current)
		{
			const bool bound(
				std::any_of(entries_.begin(), entries_.end(),
					[&d](const entry& e) { return e.device.id == d.id; }));

			if (bound)
				continue;

			std::unique_ptr<hid_device> hid;

			if (const auto* b = std::get_if<hid_binding>(&d.binding))
			{
				hid = open(b->path);

				if (hid == nullptr)
					continue;  // open() logged the reason.
			}

			std::unique_ptr<driver> drv(bind(d, hid));

			if (drv == nullptr)
			{
				Logger::Warning(Game::CON_CHANNEL_SYSTEM, "gamepad device {}: no driver binds {} over {}\n",
					d.id.value(), to_string(d.identity.family), to_string(d.transport));
				continue;
			}

			Logger::Print(Game::CON_CHANNEL_SYSTEM, "gamepad driver bound: {} over {}\n",
				to_string(d.identity.family), to_string(d.transport));

			entries_.push_back(entry{std::move(d), std::move(hid), std::move(drv)});
		}
	}

	void driver_set::submit(device_id id, const output_request& request)
	{
		for (entry& e : entries_)
		{
			if (e.device.id == id)
			{
				e.drv->submit(request);
				return;
			}
		}
	}
}
