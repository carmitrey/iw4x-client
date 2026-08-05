#include "HidDevice.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

namespace Components::GamepadControls
{
	namespace
	{
		// Largest input report any supported device produces. The DualShock 4
		// and DualSense Bluetooth reports are 78 bytes; the USB reports are
		// 64. The read buffer is sized once here so the read path never
		// allocates.
		constexpr size_t max_report_size{128};

		// Input report lengths that name a report framing for the supported
		// PlayStation families. Both families use 64-byte reports over USB
		// and 78-byte reports over Bluetooth, and HidP_GetCaps reports the
		// length including the leading report id.
		//
		// The length comes from the parsed report descriptor, which is
		// static: it is the longest input report the collection can produce,
		// whether or not the device happens to be producing it right now. A
		// Bluetooth pad still in minimal-report mode therefore advertises 78
		// just the same, which is what makes this a usable signal at
		// enumeration time, before any report has been read.
		constexpr size_t usb_input_report_length{64};
		constexpr size_t bluetooth_input_report_length{78};
	}

	// Name the report framing a device is presenting from its input report
	// length.
	//
	// The length is the framing: the two PlayStation families lay out a
	// 64-byte report one way and a 78-byte report another, differing in
	// report id, in where the common block starts, and in whether a trailing
	// CRC-32 is present. That layout is the only thing `connection` selects.
	//
	// Which physical bus the pad sits on is a separate question, and one
	// nothing here asks. Do not be tempted to corroborate the framing
	// against the device interface path: the path does not reliably carry
	// the Bluetooth HID service UUID even for a pad reached over Bluetooth,
	// and a wireless adapter presents a USB path for a pad speaking the
	// Bluetooth framing regardless. The length answers the question actually
	// being asked, and it answers it on its own.
	connection classify_link(size_t input_length) noexcept
	{
		if (input_length == bluetooth_input_report_length)
			return connection::bluetooth;

		if (input_length == usb_input_report_length)
			return connection::usb;

		return connection::unknown;
	}

	namespace
	{
		// A HID collection opened through the Windows HID class driver.
		//
		// Reads are overlapped and non-blocking: one read is kept outstanding
		// at all times and harvested on the next poll. The kernel writes into
		// buf_ for the lifetime of that outstanding read, so the buffer must
		// belong to this object and not to the caller's span -- completing a
		// read into a caller buffer that has since gone out of scope is the
		// failure mode this exists to avoid. Bytes are copied out only once
		// the read has completed.
		class windows_hid_device final : public hid_device
		{
		public:
			windows_hid_device(HANDLE handle,
				hid_attributes attributes,
				connection link,
				size_t input_length,
				size_t feature_length) noexcept
				: handle_(handle),
				attributes_(attributes),
				link_(link),
				input_length_(std::min(input_length, max_report_size)),
				feature_length_(std::min(feature_length, max_report_size))
			{
				// A manual-reset event with no name: it is signalled by the
				// kernel when an outstanding read completes and reset before
				// each new read.
				overlapped_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			}

			~windows_hid_device() override
			{
				if (read_pending_)
				{
					CancelIo(handle_);

					// Wait for the cancelled read to complete before the
					// buffer it writes into is destroyed.
					DWORD n(0);
					GetOverlappedResult(handle_, &overlapped_, &n, TRUE);
				}

				if (overlapped_.hEvent != nullptr)
					CloseHandle(overlapped_.hEvent);

				if (handle_ != INVALID_HANDLE_VALUE)
					CloseHandle(handle_);
			}

			windows_hid_device(const windows_hid_device&) = delete;
			windows_hid_device& operator=(const windows_hid_device&) = delete;

			connection link() const noexcept override { return link_; }

			hid_attributes attributes() const noexcept override { return attributes_; }

			size_t feature_report_length() const noexcept override { return feature_length_; }

			std::optional<size_t> read(std::span<std::byte> out) noexcept override
			{
				if (overlapped_.hEvent == nullptr)
					return std::nullopt;

				if (!read_pending_ && !issue_read())
					return std::nullopt;

				DWORD n(0);

				if (!GetOverlappedResult(handle_, &overlapped_, &n, FALSE))
				{
					if (GetLastError() == ERROR_IO_INCOMPLETE)
						return size_t(0);  // Still in flight; nothing new this frame.

					read_pending_ = false;
					return std::nullopt;
				}

				read_pending_ = false;

				const size_t count(std::min(static_cast<size_t>(n), out.size()));
				std::memcpy(out.data(), buf_.data(), count);

				// Keep a read outstanding so the next report is already being
				// delivered when the next frame asks for it. A failure to
				// re-arm is not an error for this call: the report we just
				// harvested is still valid, and the next read() will observe
				// the failure.
				issue_read();

				return count;
			}

			std::optional<size_t> write(std::span<const std::byte> buf) noexcept override
			{
				if (!writable_ || buf.empty())
					return std::nullopt;

				OVERLAPPED ov{};
				ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

				if (ov.hEvent == nullptr)
					return std::nullopt;

				std::optional<size_t> result;
				DWORD n(0);

				if (WriteFile(handle_,
					buf.data(),
					static_cast<DWORD>(buf.size()),
					&n,
					&ov) ||
					(GetLastError() == ERROR_IO_PENDING &&
						GetOverlappedResult(handle_, &ov, &n, TRUE)))
					result = static_cast<size_t>(n);

				CloseHandle(ov.hEvent);
				return result;
			}

			bool get_feature(std::span<std::byte> buf) noexcept override
			{
				// HidD_GetFeature sizes the transfer from the buffer, not
				// from the report id in it, and rejects anything shorter
				// than the collection's longest feature report with
				// ERROR_INVALID_USER_BUFFER -- even when the report being
				// asked for is itself shorter. Callers size against
				// feature_report_length(); a short buffer here would fail
				// every time.
				return !buf.empty() && buf.size() >= feature_length_ &&
					HidD_GetFeature(handle_,
						buf.data(),
						static_cast<ULONG>(buf.size())) != FALSE;
			}

			bool set_feature(std::span<const std::byte> buf) noexcept override
			{
				// HidD_SetFeature takes a non-const buffer but does not
				// modify it.
				return writable_ && !buf.empty() &&
					HidD_SetFeature(handle_,
						const_cast<std::byte*>(buf.data()),
						static_cast<ULONG>(buf.size())) != FALSE;
			}

			void writable(bool w) noexcept { writable_ = w; }

		private:
			// Start one overlapped read. Returns whether a read is now
			// outstanding (or completed synchronously, which
			// GetOverlappedResult also reports).
			bool issue_read() noexcept
			{
				assert(!read_pending_);

				ResetEvent(overlapped_.hEvent);
				overlapped_.Internal = 0;
				overlapped_.InternalHigh = 0;

				DWORD n(0);

				if (ReadFile(handle_,
					buf_.data(),
					static_cast<DWORD>(input_length_),
					&n,
					&overlapped_))
				{
					read_pending_ = true;
					return true;
				}

				if (GetLastError() == ERROR_IO_PENDING)
				{
					read_pending_ = true;
					return true;
				}

				return false;
			}

			HANDLE         handle_;
			hid_attributes attributes_;
			connection     link_;
			size_t         input_length_;
			size_t         feature_length_;

			bool       writable_{true};
			bool       read_pending_{false};
			OVERLAPPED overlapped_{};

			std::array<std::byte, max_report_size> buf_{};
		};

		// Read the interface path of one enumerated device interface.
		//
		// SetupDiGetDeviceInterfaceDetailW is the usual two-call idiom: the
		// first call reports the required size and fails with
		// ERROR_INSUFFICIENT_BUFFER.
		std::optional<std::wstring> interface_path(HDEVINFO set, SP_DEVICE_INTERFACE_DATA& iface)
		{
			DWORD size(0);
			SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);

			if (size == 0)
				return std::nullopt;

			std::vector<uint8_t> storage(size);

			auto* detail(
				reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data()));
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

			if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr))
				return std::nullopt;

			return std::wstring(detail->DevicePath);
		}

		// Open a device interface without asking for read or write access.
		//
		// A controller that another process holds open exclusively still
		// answers HidD_GetAttributes and HidP_GetCaps through such a handle,
		// so enumeration can identify every present device without
		// contending for it.
		HANDLE open_for_query(const std::wstring& path) noexcept
		{
			return CreateFileW(path.c_str(),
				0,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr,
				OPEN_EXISTING,
				0,
				nullptr);
		}

		// Report lengths and usage of the top-level collection behind a
		// handle.
		//
		// The feature length is carried because HidD_GetFeature will not
		// accept a buffer shorter than the collection's longest feature
		// report, whatever the report actually being asked for is; see
		// windows_hid_device::get_feature.
		struct collection_caps
		{
			size_t   input_report_length;
			size_t   feature_report_length;
			uint16_t usage_page;
			uint16_t usage;
		};

		std::optional<collection_caps> query_caps(HANDLE h) noexcept
		{
			PHIDP_PREPARSED_DATA pp(nullptr);

			if (!HidD_GetPreparsedData(h, &pp))
				return std::nullopt;

			HIDP_CAPS caps{};
			const bool ok(HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS);
			HidD_FreePreparsedData(pp);

			if (!ok)
				return std::nullopt;

			return collection_caps{caps.InputReportByteLength,
				caps.FeatureReportByteLength,
				caps.UsagePage,
				caps.Usage};
		}

		// HID usage page 0x01 (generic desktop), usage 0x05 (game pad). The
		// PlayStation controllers expose several HID collections; only the
		// game pad collection carries the input reports the drivers decode,
		// and skipping the others is what keeps a keyboard or a
		// vendor-defined collection from ever being opened.
		constexpr uint16_t usage_page_generic_desktop{0x01};
		constexpr uint16_t usage_game_pad{0x05};
	}

	std::vector<hid_enumeration_entry> enumerate()
	{
		std::vector<hid_enumeration_entry> found;

		GUID hid_guid{};
		HidD_GetHidGuid(&hid_guid);

		HDEVINFO set(SetupDiGetClassDevsW(&hid_guid,
			nullptr,
			nullptr,
			DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));

		if (set == INVALID_HANDLE_VALUE)
		{
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "unable to enumerate HID device interfaces\n");
			return found;
		}

		SP_DEVICE_INTERFACE_DATA iface{};
		iface.cbSize = sizeof(iface);

		for (DWORD i(0);
			SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, i, &iface);
			++i)
		{
			std::optional<std::wstring> path(interface_path(set, iface));

			if (!path)
				continue;

			HANDLE h(open_for_query(*path));

			if (h == INVALID_HANDLE_VALUE)
				continue;

			HIDD_ATTRIBUTES attrs{};
			attrs.Size = sizeof(attrs);

			std::optional<collection_caps> caps;

			if (HidD_GetAttributes(h, &attrs))
				caps = query_caps(h);

			CloseHandle(h);

			if (!caps)
				continue;

			if (caps->usage_page != usage_page_generic_desktop ||
				caps->usage != usage_game_pad)
				continue;

			const vendor_id vendor(attrs.VendorID);
			const product_id product(attrs.ProductID);

			if (classify(vendor, product) == family::unknown)
				continue;

			const connection link(classify_link(caps->input_report_length));

			if (link == connection::unknown)
			{
				// The device is one we drive, but it is presenting a report
				// we have no layout for. Log it and move on: decoding it
				// would mean choosing a framing on a coin toss.
				Logger::Warning(Game::CON_CHANNEL_SYSTEM,
					"HID device {} reports an input report of {} bytes; the drivers decode "
					"64-byte (USB) and 78-byte (Bluetooth) framing, so no driver is bound\n",
					to_string(classify(vendor, product)), caps->input_report_length);
				continue;
			}

			found.push_back(
				hid_enumeration_entry{
					std::move(*path),
					hid_attributes{vendor, product, attrs.VersionNumber},
					link,
					caps->input_report_length});
		}

		SetupDiDestroyDeviceInfoList(set);
		return found;
	}

	std::unique_ptr<hid_device> open(const std::wstring& path)
	{
		bool writable(true);

		HANDLE h(CreateFileW(path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			nullptr));

		if (h == INVALID_HANDLE_VALUE)
		{
			writable = false;

			h = CreateFileW(path.c_str(),
				GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr,
				OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED,
				nullptr);
		}

		if (h == INVALID_HANDLE_VALUE)
		{
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "unable to open a HID device interface\n");
			return nullptr;
		}

		// Re-establish the facts from the handle we are actually going to
		// read through. The device may have been detached since it was
		// enumerated, and the report length sizes every read.
		HIDD_ATTRIBUTES attrs{};
		attrs.Size = sizeof(attrs);

		std::optional<collection_caps> caps;

		if (HidD_GetAttributes(h, &attrs))
			caps = query_caps(h);

		if (!caps)
		{
			CloseHandle(h);
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "opened HID device does not answer its capabilities\n");
			return nullptr;
		}

		const vendor_id vendor(attrs.VendorID);
		const product_id product(attrs.ProductID);
		const connection link(classify_link(caps->input_report_length));

		if (classify(vendor, product) == family::unknown ||
			link == connection::unknown)
		{
			CloseHandle(h);
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "opened HID device is not a controller we can decode\n");
			return nullptr;
		}

		auto d(std::make_unique<windows_hid_device>(
			h,
			hid_attributes{vendor, product, attrs.VersionNumber},
			link,
			caps->input_report_length,
			caps->feature_report_length));

		d->writable(writable);

		if (!writable)
			Logger::Print(Game::CON_CHANNEL_SYSTEM, "HID device opened read-only; output reports unavailable\n");

		return d;
	}
}
