#pragma once

#include "Gamepad/CalibrationMeasure.hpp"
#include "Gamepad/CalibrationProfile.hpp"
#include "Gamepad/CalibrationStore.hpp"
#include "Gamepad/DeviceRegistry.hpp"
#include "Gamepad/DeviceDiscovery.hpp"
#include "Gamepad/DriverSet.hpp"
#include "Gamepad/XInputModule.hpp"

#include <chrono>

namespace Components
{
	// Live stick calibration, reachable via the "gpad_calibrate" console
	// command.
	//
	// Wholly self-contained: it owns its own transport/registry/discovery/
	// driver_set instances rather than reusing the legacy
	// GamepadControls::Controller path, so it does not modify or depend on
	// Controller.cpp/Gamepad.cpp/XInputGamepad.cpp/DualSenseGamepad.cpp -
	// wiring the ported driver stack into that live frame path is a later
	// task. This component exists to make the calibration pipeline
	// (Gamepad/Calibration*.hpp) exercisable end to end today.
	class GamepadCalibration : public Component
	{
	public:
		GamepadCalibration();

	private:
		enum class Phase
		{
			idle,
			rest,
			sweep,
		};

		void Tick(Game::usercmd_s*);
		void OnCalibrateCommand(const Command::Params*);
		void StartSession(GamepadControls::stick, std::chrono::milliseconds sweep_duration);
		void FinishSession();

		GamepadControls::xinput_module xinput_;
		GamepadControls::registry registry_;
		GamepadControls::discovery discovery_;
		GamepadControls::driver_set drivers_;
		GamepadControls::store calibrationStore_{"players/gamepad-calibration"};

		Phase phase_{Phase::idle};
		GamepadControls::stick stick_{GamepadControls::stick::left};
		GamepadControls::stick_measurer measurer_;
		GamepadControls::family family_{GamepadControls::family::unknown};
		std::chrono::milliseconds sweepDuration_{4000};
		std::chrono::steady_clock::time_point phaseEnd_;

		static constexpr std::chrono::milliseconds RestDuration{1000};
	};
}
