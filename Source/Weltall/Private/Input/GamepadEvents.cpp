#include "Input/GamepadEvents.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	using Weltall::EGamepadControl;

	/** A stick's or trigger's value, with the dead zone outside which it is sent though it did not move. */
	struct FAnalog
	{
		FGamepadKeyNames::Type Key;
		float Weltall::FGamepadReading::*Value;
		float DeadZone;
	};
}

FGamepadEvents::FGamepadEvents()
{
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("InitialButtonRepeatDelay"), InitialRepeatDelay, GInputIni);
	GConfig->GetFloat(TEXT("/Script/Engine.InputSettings"), TEXT("ButtonRepeatDelay"), RepeatDelay, GInputIni);
}

const std::array<FGamepadKeyNames::Type, Weltall::GamepadControlCount>& FGamepadEvents::Keys()
{
	static const std::array<FGamepadKeyNames::Type, Weltall::GamepadControlCount> Names = {FGamepadKeyNames::FaceButtonBottom,
		FGamepadKeyNames::FaceButtonRight, FGamepadKeyNames::FaceButtonLeft, FGamepadKeyNames::FaceButtonTop, FGamepadKeyNames::LeftShoulder,
		FGamepadKeyNames::RightShoulder, FGamepadKeyNames::SpecialRight, FGamepadKeyNames::SpecialLeft, FGamepadKeyNames::LeftThumb,
		FGamepadKeyNames::RightThumb, FGamepadKeyNames::LeftTriggerThreshold, FGamepadKeyNames::RightTriggerThreshold, FGamepadKeyNames::DPadUp,
		FGamepadKeyNames::DPadDown, FGamepadKeyNames::DPadLeft, FGamepadKeyNames::DPadRight, FGamepadKeyNames::LeftStickUp,
		FGamepadKeyNames::LeftStickDown, FGamepadKeyNames::LeftStickLeft, FGamepadKeyNames::LeftStickRight, FGamepadKeyNames::RightStickUp,
		FGamepadKeyNames::RightStickDown, FGamepadKeyNames::RightStickLeft, FGamepadKeyNames::RightStickRight};
	return Names;
}

void FGamepadEvents::Send(FGenericApplicationMessageHandler& Handler, FPlatformUserId User, FInputDeviceId Device, const Weltall::FGamepadReading& Reading, double Now)
{
	using Weltall::FGamepadReading;
	static const FAnalog Analogs[] = {
		{FGamepadKeyNames::LeftAnalogX, &FGamepadReading::LeftX, Weltall::GamepadLeftStickDeadZone},
		{FGamepadKeyNames::LeftAnalogY, &FGamepadReading::LeftY, Weltall::GamepadLeftStickDeadZone},
		{FGamepadKeyNames::RightAnalogX, &FGamepadReading::RightX, Weltall::GamepadRightStickDeadZone},
		{FGamepadKeyNames::RightAnalogY, &FGamepadReading::RightY, Weltall::GamepadRightStickDeadZone},
		{FGamepadKeyNames::LeftTriggerAnalog, &FGamepadReading::LeftTrigger, Weltall::GamepadTriggerThreshold},
		{FGamepadKeyNames::RightTriggerAnalog, &FGamepadReading::RightTrigger, Weltall::GamepadTriggerThreshold},
	};
	for (const FAnalog& Analog : Analogs)
	{
		const float Value = Reading.*Analog.Value;
		if (Value != Last.*Analog.Value || FMath::Abs(Value) > Analog.DeadZone)
		{
			Handler.OnControllerAnalog(Analog.Key, User, Device, Value);
		}
	}
	Last = Reading;
	const uint32 NowHeld = Weltall::HeldGamepadControls(Reading);
	for (int32 Control = 0; Control < Weltall::GamepadControlCount; ++Control)
	{
		const uint32 Bit = 1u << Control;
		const FGamepadKeyNames::Type& Key = Keys()[static_cast<std::size_t>(Control)];
		double& Repeat = NextRepeat[static_cast<std::size_t>(Control)];
		if ((NowHeld & Bit) != 0 && (Held & Bit) == 0)
		{
			Handler.OnControllerButtonPressed(Key, User, Device, false);
			Repeat = Now + InitialRepeatDelay;
		}
		else if ((NowHeld & Bit) == 0 && (Held & Bit) != 0)
		{
			Handler.OnControllerButtonReleased(Key, User, Device, false);
		}
		else if ((NowHeld & Bit) != 0 && Repeat <= Now)
		{
			Handler.OnControllerButtonPressed(Key, User, Device, true);
			Repeat = Now + RepeatDelay;
		}
	}
	Held = NowHeld;
}

void FGamepadEvents::Release(FGenericApplicationMessageHandler& Handler, FPlatformUserId User, FInputDeviceId Device)
{
	Send(Handler, User, Device, Weltall::FGamepadReading{}, 0.0);
}
