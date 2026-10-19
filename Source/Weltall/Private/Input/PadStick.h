#pragma once

#include "CoreMinimal.h"
#include "Input/ControlBindings.h"
#include "Input/PadState.h"
#include "InputCoreTypes.h"

/**
 * A gamepad's stick as the game's pad reports it, a DualShock's in analog mode: the stick the bindings' directions
 * are on, its tilt as the report's bytes, and the directions only it holds.
 */
namespace PadStick
{
	/** The stick a key belongs to, one of its axes or its four direction keys: 1 the left, 2 the right, 0 none. */
	int32 StickOf(const FKey& Key);

	/** The stick the bindings' directions are on: the first of their keys that belongs to one, 0 for none. */
	int32 BoundStick(const Weltall::FControlBindings& Bindings);

	/** A stick's horizontal and vertical axes. */
	TPair<FKey, FKey> Axes(int32 Stick);

	/** The stick's tilt (each axis -1 to 1, up and right positive) into the pad's report; none within the dead zone. */
	void Report(float X, float Y, Weltall::FPadState& Pad);

	/** The four directions' controls. */
	inline constexpr Weltall::EControlAction Directions[] = {Weltall::EControlAction::Up, Weltall::EControlAction::Down,
		Weltall::EControlAction::Left, Weltall::EControlAction::Right};

	/** The directions among Pad's held buttons that none of their keys but a stick's holds, IsDown saying whether a key is held. */
	template <typename FIsDown>
	uint16 OnlyStickHolds(const Weltall::FControlBindings& Bindings, Weltall::FPadState Pad, FIsDown&& IsDown)
	{
		uint16 Held = 0;
		for (const Weltall::EControlAction Direction : Directions)
		{
			const uint16 Bit = Weltall::FPadState::Bit(*Weltall::PadButtonOf(Direction));
			if ((Pad.Buttons & Bit) == 0)
			{
				continue;
			}
			bool bOther = false;
			for (const std::string& Binding : Bindings.Keys(Direction))
			{
				const std::string_view Name = Weltall::BoundKeyName(Binding);
				const FKey Key(FName(static_cast<int32>(Name.size()), Name.data()));
				bOther = bOther || (StickOf(Key) == 0 && IsDown(Key));
			}
			Held |= bOther ? 0 : Bit;
		}
		return Held;
	}
}
