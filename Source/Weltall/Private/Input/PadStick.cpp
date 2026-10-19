#include "Input/PadStick.h"

namespace
{
	/** A stick's travel the player's resting thumb does not reach, as the directions' own. */
	constexpr float DeadZone = 0.2f;

	/** An axis from -1 to 1 as a report's byte, 0 to 0xFF with 0x80 its centre. */
	uint8 ReportByte(float Value)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::FloorToInt(Weltall::FPadState::StickCentre + Value * 128.0f), 0, 0xFF));
	}
}

namespace PadStick
{
	int32 StickOf(const FKey& Key)
	{
		if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY || Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down
			|| Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right)
		{
			return 1;
		}
		if (Key == EKeys::Gamepad_RightX || Key == EKeys::Gamepad_RightY || Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down
			|| Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right)
		{
			return 2;
		}
		return 0;
	}

	int32 BoundStick(const Weltall::FControlBindings& Bindings)
	{
		for (const Weltall::EControlAction Direction : Directions)
		{
			for (const std::string& Binding : Bindings.Keys(Direction))
			{
				const std::string_view Name = Weltall::BoundKeyName(Binding);
				if (const int32 Stick = StickOf(FKey(FName(static_cast<int32>(Name.size()), Name.data()))))
				{
					return Stick;
				}
			}
		}
		return 0;
	}

	TPair<FKey, FKey> Axes(int32 Stick)
	{
		return Stick == 2 ? TPair<FKey, FKey>(EKeys::Gamepad_RightX, EKeys::Gamepad_RightY) : TPair<FKey, FKey>(EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY);
	}

	void Report(float X, float Y, Weltall::FPadState& Pad)
	{
		if (X * X + Y * Y < DeadZone * DeadZone)
		{
			X = 0.0f;
			Y = 0.0f;
		}
		Pad.StickX = ReportByte(X);
		// The report's vertical axis grows downward.
		Pad.StickY = ReportByte(-Y);
	}
}
