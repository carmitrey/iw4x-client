#pragma once

#include "CalibrationProfile.hpp"

#include <optional>
#include <string>

namespace Components::GamepadControls
{
	// Persists calibration profiles to disk, one per file.
	//
	// A profile is stored in a small versioned text file under the
	// store's directory, named by family and, when available, device
	// key. Because the file name encodes both, and load() additionally
	// checks that the file's own family and key match what was asked
	// for, a profile is never applied to an unrelated device. Loading
	// validates the profile and rejects an unsupported version or
	// malformed content, reporting the reason; a missing profile is not
	// an error and simply yields nullopt so the caller uses the default.
	//
	// Uses this codebase's Utils::IO (string-path based) rather than
	// std::fstream/std::filesystem, matching the convention already used
	// for other per-user data (e.g. Node.cpp's players/nodes.json).
	class store
	{
	public:
		explicit store(std::string directory);

		std::optional<profile> load(family, std::optional<uint64_t> device_key) const;

		bool save(const profile&) const;

		const std::string& directory() const noexcept { return dir_; }

	private:
		std::string file_for(family, std::optional<uint64_t> device_key) const;

		std::string dir_;
	};
}
