#include "GamepadCalibration.hpp"

#include "Events.hpp"
#include "Gamepad/CalibrationValidate.hpp"

#include <cstdlib>

namespace Components
{
	GamepadCalibration::GamepadCalibration()
		: discovery_(registry_, xinput_)
		, drivers_(xinput_)
	{
		Command::Add("gpad_calibrate", [this](const Command::Params* params)
			{
				this->OnCalibrateCommand(params);
			});

		Events::OnClientKeyMove([this](Game::usercmd_s* cmd)
			{
				this->Tick(cmd);
			});
	}

	void GamepadCalibration::OnCalibrateCommand(const Command::Params* params)
	{
		if (params->size() < 2)
		{
			Game::Com_Printf(0, "USAGE: gpad_calibrate <left|right> [seconds]\n");
			return;
		}

		const std::string which(params->get(1));

		GamepadControls::stick s(GamepadControls::stick::left);
		if (which == "left")
			s = GamepadControls::stick::left;
		else if (which == "right")
			s = GamepadControls::stick::right;
		else
		{
			Game::Com_Printf(0, "USAGE: gpad_calibrate <left|right> [seconds]\n");
			return;
		}

		int seconds(4);
		if (params->size() >= 3)
			seconds = std::atoi(params->get(2));
		if (seconds < 1)
			seconds = 1;

		StartSession(s, std::chrono::seconds(seconds));
	}

	void GamepadCalibration::StartSession(GamepadControls::stick s, std::chrono::milliseconds sweep_duration)
	{
		stick_ = s;
		sweepDuration_ = sweep_duration;
		measurer_.reset();
		family_ = GamepadControls::family::unknown;

		// Reconcile immediately rather than waiting on the discovery
		// interval, so a session that just started already has a device
		// to sample from on its very first tick.
		discovery_.scan_now();
		drivers_.reconcile(registry_);

		phase_ = Phase::rest;
		phaseEnd_ = std::chrono::steady_clock::now() + RestDuration;

		Game::Com_Printf(0, "gpad_calibrate: hold the %s stick at rest...\n", GamepadControls::to_string(stick_));
	}

	void GamepadCalibration::Tick(Game::usercmd_s*)
	{
		if (phase_ == Phase::idle)
			return;

		discovery_.scan();
		drivers_.reconcile(registry_);

		bool sampled(false);
		drivers_.for_each([this, &sampled](GamepadControls::driver& drv, const GamepadControls::device_connection&)
			{
				if (sampled)
					return;

				GamepadControls::raw_sample raw{};
				GamepadControls::canonical_sample canonical{};
				if (!drv.poll(raw, canonical))
					return;

				sampled = true;
				family_ = drv.family();

				const GamepadControls::stick_sample& s(canonical.sticks[static_cast<size_t>(stick_)]);
				if (phase_ == Phase::rest)
					measurer_.observe_rest(s.normalized);
				else
					measurer_.observe_sweep(s.normalized);
			});

		if (std::chrono::steady_clock::now() < phaseEnd_)
			return;

		if (phase_ == Phase::rest)
		{
			phase_ = Phase::sweep;
			phaseEnd_ = std::chrono::steady_clock::now() + sweepDuration_;
			Game::Com_Printf(0, "gpad_calibrate: now sweep the %s stick around its full range...\n", GamepadControls::to_string(stick_));
		}
		else
		{
			FinishSession();
		}
	}

	void GamepadCalibration::FinishSession()
	{
		phase_ = Phase::idle;

		if (family_ == GamepadControls::family::unknown)
		{
			Game::Com_Printf(0, "gpad_calibrate: no gamepad was detected during calibration\n");
			return;
		}

		const GamepadControls::stick_calibration measured(measurer_.finalize());

		GamepadControls::profile p(GamepadControls::default_profile(family_));
		p.source = GamepadControls::value_source::measured;
		p.sticks[static_cast<size_t>(stick_)] = measured;

		std::string why;
		if (!GamepadControls::validate(p, why))
		{
			Game::Com_Printf(0, "gpad_calibrate: measured profile failed validation: %s\n", why.c_str());
			return;
		}

		if (calibrationStore_.save(p))
			Game::Com_Printf(0, "gpad_calibrate: saved (center %.3f,%.3f range %.3f,%.3f)\n",
				measured.center_x, measured.center_y, measured.range_x, measured.range_y);
		else
			Game::Com_Printf(0, "gpad_calibrate: failed to save calibration profile\n");
	}
}
