#include "DeviceRegistry.hpp"

#include <algorithm>

namespace Components::GamepadControls
{
	device_id registry::add(device_identity identity,
		transport_kind t,
		connection link,
		capabilities caps,
		transport_binding binding)
	{
		device_id id;

		{
			std::lock_guard<std::mutex> l(mutex_);

			// Re-observing a present device refreshes its facts in place
			// rather than creating a duplicate. This branch releases the
			// lock on return.
			for (device_connection& d : devices_)
			{
				if (same_binding(d.binding, binding))
				{
					d.identity = identity;
					d.transport = t;
					d.link = link;
					d.caps = caps;
					return d.id;
				}
			}

			id = device_id(next_++);
			devices_.push_back(
				device_connection{id, identity, t, link, caps, std::move(binding)});

			generation_.fetch_add(1);
		}

		// Log the arrival with the lock released so a slow sink cannot
		// stall a concurrent frame reading the registry.
		Logger::Print(Game::CON_CHANNEL_SYSTEM, "gamepad device connected: {} over {}/{}\n",
			to_string(identity.family), to_string(t), to_string(link));
		return id;
	}

	bool registry::remove(device_id id)
	{
		family f{family::unknown};

		{
			std::lock_guard<std::mutex> l(mutex_);

			auto i(std::find_if(devices_.begin(), devices_.end(),
				[id](const device_connection& d) { return d.id == id; }));

			if (i == devices_.end())
				return false;

			f = i->identity.family;
			devices_.erase(i);

			generation_.fetch_add(1);
		}

		Logger::Print(Game::CON_CHANNEL_SYSTEM, "gamepad device disconnected: {}\n", to_string(f));
		return true;
	}

	std::optional<device_connection> registry::find(device_id id) const
	{
		std::lock_guard<std::mutex> l(mutex_);

		for (const device_connection& d : devices_)
		{
			if (d.id == id)
				return d;
		}

		return std::nullopt;
	}

	size_t registry::size() const
	{
		std::lock_guard<std::mutex> l(mutex_);
		return devices_.size();
	}
}
