#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "Input/GamepadEvents.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Keeps the gamepad events sent, as "+Key" for a press, "*Key" for a repeat, "-Key" for a release and "Key=value" for a value. */
	class FGamepadEventsHeard final : public FGenericApplicationMessageHandler
	{
	public:
		virtual bool OnControllerAnalog(FGamepadKeyNames::Type Key, FPlatformUserId User, FInputDeviceId Device, float Value) override
		{
			Heard.Add(FString::Printf(TEXT("%s=%.2f"), *Key.ToString(), Value));
			return true;
		}

		virtual bool OnControllerButtonPressed(FGamepadKeyNames::Type Key, FPlatformUserId User, FInputDeviceId Device, bool bRepeat) override
		{
			Heard.Add((bRepeat ? TEXT("*") : TEXT("+")) + Key.ToString());
			return true;
		}

		virtual bool OnControllerButtonReleased(FGamepadKeyNames::Type Key, FPlatformUserId User, FInputDeviceId Device, bool bRepeat) override
		{
			Heard.Add(TEXT("-") + Key.ToString());
			return true;
		}

		/** What was heard since the last call, joined by spaces. */
		FString Take()
		{
			const FString Joined = FString::Join(Heard, TEXT(" "));
			Heard.Reset();
			return Joined;
		}

		TArray<FString> Heard;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGamepadEventsTest, "Weltall.Input.GamepadReadingsAreSentAsXInputSendsThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGamepadEventsTest::RunTest(const FString&)
{
	using Weltall::EGamepadControl;
	FGamepadEventsHeard Handler;
	FGamepadEvents Events;
	const FPlatformUserId User = FPlatformUserId::CreateFromInternalId(0);
	const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(7);
	const FString Cross = FGamepadKeyNames::FaceButtonBottom.ToString();
	Weltall::FGamepadReading Reading;
	Events.Send(Handler, User, Device, Reading, 1.0);
	TestEqual(TEXT("At rest nothing is sent"), Handler.Take(), FString());
	TestFalse(TEXT("nothing held"), Events.IsHeld());

	// Cross pressed, repeated after 0.2 seconds and every 0.1 after that, then let go.
	Reading.Buttons = Weltall::GamepadControlBit(EGamepadControl::FaceBottom);
	Events.Send(Handler, User, Device, Reading, 1.0);
	TestEqual(TEXT("Cross pressed"), Handler.Take(), TEXT("+") + Cross);
	TestTrue(TEXT("held"), Events.IsHeld());
	Events.Send(Handler, User, Device, Reading, 1.15);
	TestEqual(TEXT("not yet repeated"), Handler.Take(), FString());
	Events.Send(Handler, User, Device, Reading, 1.201);
	TestEqual(TEXT("repeated"), Handler.Take(), TEXT("*") + Cross);
	Events.Send(Handler, User, Device, Reading, 1.29);
	TestEqual(TEXT("not again yet"), Handler.Take(), FString());
	Events.Send(Handler, User, Device, Reading, 1.302);
	TestEqual(TEXT("again"), Handler.Take(), TEXT("*") + Cross);
	Reading.Buttons = 0;
	Events.Send(Handler, User, Device, Reading, 1.31);
	TestEqual(TEXT("let go"), Handler.Take(), TEXT("-") + Cross);

	// The left stick pushed left: its value while it stands outside the dead zone, its direction as a key; back at rest once.
	Reading.LeftX = -1.0f;
	Events.Send(Handler, User, Device, Reading, 2.0);
	TestEqual(TEXT("stick pushed"), Handler.Take(), FString::Printf(TEXT("%s=-1.00 +%s"), *FGamepadKeyNames::LeftAnalogX.ToString(), *FGamepadKeyNames::LeftStickLeft.ToString()));
	Events.Send(Handler, User, Device, Reading, 2.01);
	TestEqual(TEXT("its value again while it stays out"), Handler.Take(), FString::Printf(TEXT("%s=-1.00"), *FGamepadKeyNames::LeftAnalogX.ToString()));
	Reading.LeftX = 0.1f;
	Events.Send(Handler, User, Device, Reading, 2.02);
	TestEqual(TEXT("back within the dead zone"), Handler.Take(), FString::Printf(TEXT("%s=0.10 -%s"), *FGamepadKeyNames::LeftAnalogX.ToString(), *FGamepadKeyNames::LeftStickLeft.ToString()));
	Events.Send(Handler, User, Device, Reading, 2.03);
	TestEqual(TEXT("still within it: nothing"), Handler.Take(), FString());

	// A trigger past its threshold is its key too; a gamepad that leaves lets go of everything.
	Reading.LeftX = 0.0f;
	Reading.RightTrigger = 0.5f;
	Reading.Buttons = Weltall::GamepadControlBit(EGamepadControl::DPadUp);
	Events.Send(Handler, User, Device, Reading, 3.0);
	TestEqual(TEXT("trigger and d-pad"), Handler.Take(),
		FString::Printf(TEXT("%s=0.00 %s=0.50 +%s +%s"), *FGamepadKeyNames::LeftAnalogX.ToString(), *FGamepadKeyNames::RightTriggerAnalog.ToString(),
			*FGamepadKeyNames::RightTriggerThreshold.ToString(), *FGamepadKeyNames::DPadUp.ToString()));
	Events.Release(Handler, User, Device);
	TestEqual(TEXT("released"), Handler.Take(),
		FString::Printf(TEXT("%s=0.00 -%s -%s"), *FGamepadKeyNames::RightTriggerAnalog.ToString(), *FGamepadKeyNames::RightTriggerThreshold.ToString(),
			*FGamepadKeyNames::DPadUp.ToString()));
	TestFalse(TEXT("nothing held after"), Events.IsHeld());
	return true;
}

#endif
