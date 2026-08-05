#include "AimTypes.hpp"

namespace Components::GamepadControls
{
	std::ostream& operator<<(std::ostream& os, degrees d)
	{
		return os << d.value << "deg";
	}

	std::ostream& operator<<(std::ostream& os, deg_per_s r)
	{
		return os << r.value << "deg/s";
	}
}
