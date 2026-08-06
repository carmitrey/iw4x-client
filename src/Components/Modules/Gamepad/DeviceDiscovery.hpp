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

		// Whether scan()/scan_now() enumerate HID devices at all. Defaults
		// to true. A caller that only ever wants XInput-family devices
		// (matching gpad_force_xinput_only) can disable this to skip HID
		// enumeration outright rather than just throttling it (IW-2.8).
		void set_hid_enumeration_enabled(bool enabled) noexcept { hid_enabled_ = enabled; }

		static constexpr std::chrono::milliseconds interval{1000};

		// HID enumeration (scan_hid(), via enumerate() in HidDevice.cpp)
		// walks every HID interface the OS reports -- keyboard, mouse, any
		// USB HID peripheral, not just game controllers -- opening a handle
		// and querying each one through SetupAPI. Measured under some Wine/
		// Proton configurations to be expensive enough that running it every
		// `interval` on the main thread stalls the frame loop badly enough
		// to look like a hang (IW-2.8 finding, live-hardware testing).
		// scan()'s periodic path therefore re-enumerates HID far less often
		// than it re-checks XInput (which is a handful of cheap
		// XInputGetCapabilities calls); scan_now() -- the startup and
		// device-change-notification path -- still does both immediately,
		// since that path is rare by construction, not a per-frame cost.
		static constexpr std::chrono::milliseconds hid_interval{30000};

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
		bool              hid_enabled_{true};

		bool                                  scanned_{false};
		std::chrono::steady_clock::time_point last_scan_{};
		std::chrono::steady_clock::time_point last_hid_scan_{};
	};
}
