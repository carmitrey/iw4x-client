#include "AimDeadzone.hpp"
#include "TransportGamepadAPI.hpp"

#define PUBLIC_GET_PRIVATE_SET(type, name) \
		private:\
			type name;\
		public:\
			type get_##name () const { return name; }

// type (*name(void))dimensions {\  won't work :(
#define PUBLIC_GET_PRIVATE_SET_ARRAY(type, name, dimensions) \
		private:\
			type name dimensions;\
		public:\
			const type* get_##name() {\
		return name; \
}

namespace Components::GamepadControls
{
	class Controller
	{

	public:
		static void InitializeDvars();

		bool PlugIn(uint8_t);

		void SetLowRumble(double rumble);

		void SetHighRumble(double rumble);

		void SetForceFeedback(const GamepadAPI::TriggerFeedback& left, const GamepadAPI::TriggerFeedback& right);

		void StopRumbles();

		void UpdateState(bool additive=false);
		void PushUpdates();

		// Un-deadzoned, un-curved right-stick vector from the transport
		// backend's last sample (IW-2.8). Consumed by the aim pipeline,
		// which owns deadzone/curve shaping for aim itself so it happens
		// exactly once; GetStick(GPAD_RX/RY) below stays deadzone-remapped
		// by ApplyDeadzone as before for every other existing consumer.
		GamepadControls::stick_vector GetAimStick() const;

		// Forward an OS device-change notification to the transport
		// backend's discovery, so its next scan runs immediately instead
		// of waiting out the interval.
		void NotifyDeviceChange() noexcept;

		// Radial deadzone parameters (inner/outer) shared with the aim
		// pipeline, built from the same gpad_stick_deadzone_min/max dvars
		// ApplyDeadzone already uses, so aim deadzone shaping matches the
		// movement deadzone's tuning without duplicating dvars.
		static GamepadControls::deadzone_params GetStickDeadzoneParams();

		// Whether the transport backend should bind only XInput-family
		// devices (IW-2.8: the same gpad_force_xinput_only contract the
		// old two-backend PlugIn used to enforce by only ever trying
		// XInputGamePadAPI, now enforced by TransportGamepadAPI filtering
		// within the registry's discovered devices instead).
		static bool GetForceXInputOnly();

		float GetStick(const Game::GamePadStick stick);
		float GetButton(Game::GamePadButton button);
		bool ButtonRequiresUpdates(Game::GamePadButton button);
		bool IsButtonReleased(Game::GamePadButton button);
		bool IsButtonPressed(Game::GamePadButton button);

		static Dvar::Var gpad_debug;

		bool inUse;

	private:

		static Dvar::Var gpad_stick_pressed_hysteresis;
		static Dvar::Var gpad_stick_pressed;
		static Dvar::Var gpad_stick_deadzone_max;
		static Dvar::Var gpad_stick_deadzone_min;
		static Dvar::Var gpad_button_deadzone;
		static Dvar::Var gpad_button_rstick_deflect_max;
		static Dvar::Var gpad_button_lstick_deflect_max;
		static Dvar::Var gpad_allow_force_feedback;
		static Dvar::Var gpad_force_xinput_only;

		void UpdateDigitals(bool additive);
		void UpdateAnalogs();
		void UpdateSticks(bool additive);
		void ApplyDeadzone(Game::vec2_t& stick);
		void UpdateSticksDown(bool additive);

		bool stickDown[4][Game::GPAD_STICK_DIR_COUNT];
		bool stickDownLast[4][Game::GPAD_STICK_DIR_COUNT];

	public:
		bool get_stickDown(int stickIndex, Game::GamePadStickDir dir) { return stickDown[stickIndex][dir];}
		bool get_stickDownLast(int stickIndex, Game::GamePadStickDir dir) { return stickDownLast[stickIndex][dir];}

		PUBLIC_GET_PRIVATE_SET(GamepadAPI::TriggerFeedback, leftForceFeedback);
		PUBLIC_GET_PRIVATE_SET(GamepadAPI::TriggerFeedback, rightForceFeedback);

		PUBLIC_GET_PRIVATE_SET(bool, enabled);
		PUBLIC_GET_PRIVATE_SET(int, portIndex);
		PUBLIC_GET_PRIVATE_SET(unsigned short, digitals);
		PUBLIC_GET_PRIVATE_SET(unsigned short, lastDigitals);
		PUBLIC_GET_PRIVATE_SET(float, lowRumble);
		PUBLIC_GET_PRIVATE_SET(float, highRumble);

		PUBLIC_GET_PRIVATE_SET_ARRAY(float, analogs, [2]);
		PUBLIC_GET_PRIVATE_SET_ARRAY(float, lastAnalogs, [2]);
		PUBLIC_GET_PRIVATE_SET_ARRAY(float, sticks, [4]);
		PUBLIC_GET_PRIVATE_SET_ARRAY(float, lastSticks, [4]);


	private:
		std::unique_ptr<GamepadControls::GamepadAPI> api;

		// Non-owning alias of api, valid whenever api is set (PlugIn always
		// constructs a TransportGamepadAPI). Kept separate from the api
		// pointer -- which stays typed as the small, backend-agnostic
		// GamepadAPI interface -- so GetAimStick()/NotifyDeviceChange()
		// don't need to widen that interface for a single backend's extra
		// accessors.
		GamepadControls::TransportGamepadAPI* transport{nullptr};
	};
}


#undef PUBLIC_GET_PRIVATE_SET
#undef PUBLIC_GET_PRIVATE_SET_ARRAY
