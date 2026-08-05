#include "CalibrationStore.hpp"

#include "CalibrationValidate.hpp"

#include <format>
#include <sstream>

namespace Components::GamepadControls
{
	namespace
	{
		constexpr const char* magic{"iw4x-controller-calibration"};

		// The highest family enumerator, so a family read from a file can
		// be range checked before it is cast back.
		constexpr int family_max{static_cast<int>(family::dualsense_edge)};

		constexpr int source_max{static_cast<int>(value_source::user)};
	}

	store::store(std::string directory)
		: dir_(std::move(directory))
	{
	}

	std::string store::file_for(family f, std::optional<uint64_t> device_key) const
	{
		// Encode the family and, when present, the device key into the
		// name so two devices, or two families, never share a file.
		std::string name(device_key
				? std::format("controller-{}-{:016x}.cal", to_string(f), *device_key)
				: std::format("controller-{}.cal", to_string(f)));

		return dir_ + "/" + name;
	}

	bool store::save(const profile& p) const
	{
		std::string why;
		if (!validate(p, why))
		{
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "refusing to save an invalid gamepad calibration profile: {}\n", why);
			return false;
		}

		Utils::IO::CreateDir(dir_);  // Best effort; WriteFile below reports failure.

		std::ostringstream os;
		os.precision(9);  // max_digits10 for float, so a value survives the write/read round trip exactly.

		os << magic << ' ' << p.version << '\n';
		os << "family " << static_cast<int>(p.family) << '\n';
		os << "source " << static_cast<int>(p.source) << '\n';
		os << "device_key " << (p.device_key ? 1 : 0) << ' ' << (p.device_key ? *p.device_key : uint64_t{0}) << '\n';

		for (size_t i(0); i < stick_count; ++i)
		{
			const stick_calibration& s(p.sticks[i]);
			os << "stick " << i << ' '
				<< s.center_x << ' ' << s.center_y << ' '
				<< s.range_x << ' ' << s.range_y << ' '
				<< s.drift_threshold << '\n';
		}

		for (size_t i(0); i < trigger_count; ++i)
		{
			const trigger_calibration& t(p.triggers[i]);
			os << "trigger " << i << ' ' << t.min << ' ' << t.max << '\n';
		}

		os << "motion "
			<< p.motion.gyro_bias.x << ' ' << p.motion.gyro_bias.y << ' ' << p.motion.gyro_bias.z << ' '
			<< p.motion.accel_bias.x << ' ' << p.motion.accel_bias.y << ' ' << p.motion.accel_bias.z << ' '
			<< p.motion.gyro_scale << ' ' << p.motion.accel_scale << '\n';

		os << "smoothing " << p.smoothing << '\n';

		if (!Utils::IO::WriteFile(file_for(p.family, p.device_key), os.str()))
		{
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "could not write gamepad calibration profile\n");
			return false;
		}

		return true;
	}

	std::optional<profile> store::load(family f, std::optional<uint64_t> device_key) const
	{
		const std::string file(file_for(f, device_key));

		std::string content;
		if (!Utils::IO::ReadFile(file, &content))
			return std::nullopt;  // No stored profile is not an error.

		std::istringstream is(content);

		auto reject = [](const char* why) -> std::optional<profile>
		{
			Logger::Warning(Game::CON_CHANNEL_SYSTEM, "gamepad calibration profile rejected: {}\n", why);
			return std::nullopt;
		};

		// Header: magic and version. An unsupported version is rejected
		// rather than reinterpreted under the current layout.
		std::string token;
		unsigned version(0);
		if (!(is >> token >> version) || token != magic)
			return reject("bad header");

		if (version == 0 || version > profile::current_version)
			return reject("unsupported version");

		profile p;
		p.version = static_cast<uint16_t>(version);

		int family_i(0);
		int source_i(0);
		int has_key(0);
		uint64_t key_value(0);

		if (!(is >> token >> family_i) || token != "family" ||
			family_i < 0 || family_i > family_max)
			return reject("bad family");

		if (!(is >> token >> source_i) || token != "source" ||
			source_i < 0 || source_i > source_max)
			return reject("bad source");

		if (!(is >> token >> has_key >> key_value) || token != "device_key")
			return reject("bad device key");

		p.family = static_cast<family>(family_i);
		p.source = static_cast<value_source>(source_i);
		if (has_key != 0)
			p.device_key = key_value;

		for (size_t i(0); i < stick_count; ++i)
		{
			size_t idx(0);
			stick_calibration s;
			if (!(is >> token >> idx >> s.center_x >> s.center_y >>
					s.range_x >> s.range_y >> s.drift_threshold) ||
				token != "stick" || idx != i)
				return reject("bad stick record");

			p.sticks[i] = s;
		}

		for (size_t i(0); i < trigger_count; ++i)
		{
			size_t idx(0);
			trigger_calibration t;
			if (!(is >> token >> idx >> t.min >> t.max) ||
				token != "trigger" || idx != i)
				return reject("bad trigger record");

			p.triggers[i] = t;
		}

		if (!(is >> token >> p.motion.gyro_bias.x >> p.motion.gyro_bias.y >>
				p.motion.gyro_bias.z >> p.motion.accel_bias.x >>
				p.motion.accel_bias.y >> p.motion.accel_bias.z >>
				p.motion.gyro_scale >> p.motion.accel_scale) ||
			token != "motion")
			return reject("bad motion record");

		if (!(is >> token >> p.smoothing) || token != "smoothing")
			return reject("bad smoothing record");

		// The file must describe the device it was requested for; a
		// mismatch means the wrong file or a corrupt one, and applying it
		// would cross devices.
		if (p.family != f || p.device_key != device_key)
			return reject("family or device key mismatch");

		std::string why;
		if (!validate(p, why))
			return reject(why.c_str());

		return p;
	}
}
