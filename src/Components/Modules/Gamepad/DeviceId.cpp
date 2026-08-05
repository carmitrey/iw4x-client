#include "DeviceId.hpp"

namespace Components::GamepadControls
{
	std::ostream& operator<<(std::ostream& os, device_id d)
	{
		// Render the null handle distinctly so diagnostics never read as if a
		// real device with id 0 existed.
		if (!d)
			return os << "device(none)";

		return os << "device(" << d.value() << ')';
	}

	const char* to_string(transport_kind t) noexcept
	{
		switch (t)
		{
		case transport_kind::unknown:   return "unknown";
		case transport_kind::xinput:    return "xinput";
		case transport_kind::raw_input: return "raw-input";
		case transport_kind::hid:       return "hid";
		}

		return "unknown";
	}

	std::ostream& operator<<(std::ostream& os, transport_kind t)
	{
		return os << to_string(t);
	}
}
