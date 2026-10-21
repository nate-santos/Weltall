#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "Input/HidGamepads.h"

#include <array>

/**
 * A gamepad's readings as the engine's gamepad events, sent as its XInput device sends them: each control's key pressed,
 * repeated while held and released, the triggers' thresholds and the sticks' directions among them, and each stick's and
 * trigger's value while it moves or stands outside its dead zone.
 */
class WELTALL_API FGamepadEvents
{
public:
	FGamepadEvents();

	/** Sends what Reading changes since the last one, at Now (seconds). */
	void Send(FGenericApplicationMessageHandler& Handler, FPlatformUserId User, FInputDeviceId Device, const Weltall::FGamepadReading& Reading, double Now);

	/** Lets go of everything, as a gamepad that leaves does. */
	void Release(FGenericApplicationMessageHandler& Handler, FPlatformUserId User, FInputDeviceId Device);

	/** Whether the last reading holds a control: the player using the gamepad. */
	bool IsHeld() const { return Held != 0; }

	/** The engine's key for each control, in EGamepadControl's order. */
	static const std::array<FGamepadKeyNames::Type, Weltall::GamepadControlCount>& Keys();

private:
	uint32 Held = 0;
	Weltall::FGamepadReading Last;
	std::array<double, Weltall::GamepadControlCount> NextRepeat{};
	float InitialRepeatDelay = 0.2f;
	float RepeatDelay = 0.1f;
};
