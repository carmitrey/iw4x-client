#pragma once

#include "DeviceRegistry.hpp"
#include "XInputModule.hpp"

#include <atomic>
#include <chrono>
#include <vector>

namespace Components::GamepadControls
{
	// Keeps the registry in step with the devices the operating system
	// reports.
	//
	// Discovery is the only writer of the registry. It answers one
	// question, what is attached right now, and answers it from the
	// transports themselves: the four XInput user slots, and the present
	// HID game pad interfaces whose USB identifiers name a family we drive.
	// It never opens a device for input, never decodes a report, and never
	// creates a driver.
	//
	// Enumerating devices is expensive relative to a frame, so a scan is
	// driven two ways: immediately when notify_device_change() has been
	// called since the last scan, and otherwise no more than once per
	// interval. The notification makes a plugged-in controller appear at
	// once; the interval is the fallback that also catches XInput arrivals,
	// which raise no HID notification, and covers a notification that was
	// missed. The steady state is that scan() observes the same devices and
	// the registry's generation does not move, so nothing downstream works.
	//
	// Unlike the 64-bit reference, this class does not own a background
	// thread pumping WM_DEVICECHANGE: this 32-bit codebase already has
	// working event-driven hotplug via Window::OnDeviceChange, running on
	// the main thread. notify_device_change() is the hook a caller (wired
	// in a later task) uses to forward that existing notification here.
	class discovery
	{
	public:
		discovery(registry&, const xinput_module&);

		// Reconcile the registry with the operating system, at most once
		// per interval. Safe to call from the engine frame; a call that the
		// interval suppresses does no work beyond a clock read.
		void scan();

		// Reconcile now, ignoring the interval. Used at startup, and by a
		// device-change notification.
		void scan_now();

		// Record that something changed and the next scan() should not wait
		// out the interval. Coarse ("something changed", not what) by
		// design: scan_now() always reconciles the full device set anyway,
		// so a caller need only forward the raw hotplug signal.
		void notify_device_change() noexcept { pending_.store(true); }

		static constexpr std::chrono::milliseconds interval{1000};

	private:
		// Record the devices each transport currently reports, appending
		// the binding of every device seen to `seen`.
		void scan_xinput(std::vector<transport_binding>& seen);
		void scan_hid(std::vector<transport_binding>& seen);

		// Drop registry records whose binding was not observed in this
		// pass.
		void retire_unseen(const std::vector<transport_binding>& seen);

		registry&             registry_;
		const xinput_module&  xinput_;

		std::atomic<bool> pending_{false};

		bool                                  scanned_{false};
		std::chrono::steady_clock::time_point last_scan_{};
	};
}
