#pragma once

#include "DeviceRegistry.hpp"
#include "Driver.hpp"
#include "HidDevice.hpp"
#include "XInputModule.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace Components::GamepadControls
{
	// The live drivers, one per registered device.
	//
	// This is where a registry record, identity and attachment, becomes a
	// driver that can actually be polled. It owns both the driver and,
	// for HID devices, the open transport the driver borrows, so that the
	// two are created and destroyed together and a driver can never
	// outlive its handle.
	//
	// Binding is by family: an Xbox-class device gets the XInput driver,
	// and each PlayStation family gets its own. There is no fallback
	// driver. A device whose family or link the device layer could not
	// establish is left unbound, because the alternative is to decode its
	// reports against a layout chosen by elimination.
	//
	// Named driver_set rather than the reference's bare "set": this
	// codebase keeps the whole subsystem flat inside
	// Components::GamepadControls, with no nested driver namespace to
	// disambiguate a one-word name.
	class driver_set
	{
	public:
		explicit driver_set(const xinput_module&);

		driver_set(const driver_set&) = delete;
		driver_set& operator=(const driver_set&) = delete;

		// Create drivers for devices that have appeared and destroy
		// drivers for devices that have gone.
		//
		// Returns immediately when the registry's membership has not
		// changed since the last call, which is the steady state on
		// every frame. The cost of that check is one atomic load.
		void reconcile(const registry&);

		// Visit each bound driver together with the device it drives.
		template <class F>
		void for_each(F&& fn)
		{
			for (entry& e : entries_)
				fn(*e.drv, e.device);
		}

		// Apply an output request to one device by id. Does nothing when
		// the id is not bound. The driver decides whether it can honor
		// the request.
		void submit(device_id, const output_request&);

		size_t size() const noexcept { return entries_.size(); }

	private:
		// One bound device. hid is null for XInput devices; when it is
		// not, drv holds a reference into it, so the two must be
		// destroyed in this order and the entry must not be copied.
		struct entry
		{
			device_connection device;
			std::unique_ptr<hid_device> hid;
			std::unique_ptr<driver> drv;
		};

		std::unique_ptr<driver> bind(const device_connection&, std::unique_ptr<hid_device>&);

		const xinput_module& xinput_;

		uint64_t generation_{0};
		bool reconciled_{false};
		std::vector<entry> entries_;
	};
}
