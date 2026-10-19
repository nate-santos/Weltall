#pragma once

#include "Input/PadState.h"

#include <array>
#include <optional>
#include <vector>

namespace Weltall
{
	/** Authored touch controls. No images, controller skins or game data are used. */
	enum class ETouchControl : uint8 { DPad, Triangle, Circle, Cross, Square, L1, R1, L2, R2, Select, Start, L3, R3, Count };
	inline constexpr std::size_t TouchControlCount = static_cast<std::size_t>(ETouchControl::Count);
	inline constexpr std::array<const char*, TouchControlCount> TouchControlNames{
		"dpad", "triangle", "circle", "cross", "square", "l1", "r1", "l2", "r2", "select", "start", "l3", "r3"};

	struct FTouchPoint
	{
		float X = 0.0f;
		float Y = 0.0f;
		bool operator==(const FTouchPoint&) const = default;
	};

	struct FTouchRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		constexpr FTouchPoint Center() const { return {X + Width * 0.5f, Y + Height * 0.5f}; }
		constexpr bool Contains(FTouchPoint Point) const
		{
			return Point.X >= X && Point.Y >= Y && Point.X < X + Width && Point.Y < Y + Height;
		}
	};

	struct FTouchOptions
	{
		float IdleOpacity = 0.25f;
		float PressedOpacity = 0.55f;
		float Scale = 1.0f;
		bool bReserveSides = false;
		/** Normalized offsets from the edge layout, retained across resolution and landscape-side changes. */
		std::array<FTouchPoint, TouchControlCount> Offsets{};
		bool operator==(const FTouchOptions&) const = default;
	};

	/** Safe-area-local geometry, independent of Unreal and the physical landscape side. */
	struct FTouchLayout
	{
		std::array<FTouchRect, TouchControlCount> Controls{};
		FTouchRect Picture;
		WELTALLCORE_API static FTouchLayout Build(float Width, float Height, const FTouchOptions& Options);
		WELTALLCORE_API std::optional<ETouchControl> Hit(FTouchPoint Point) const;
	};

	/** Per-pointer ownership prevents one finger's release from releasing another's button. */
	class FTouchPad
	{
	public:
		WELTALLCORE_API void SetLayout(const FTouchLayout& InLayout);
		WELTALLCORE_API bool Begin(int32 Pointer, FTouchPoint Point);
		WELTALLCORE_API bool Move(int32 Pointer, FTouchPoint Point);
		WELTALLCORE_API bool End(int32 Pointer);
		/** Capture cancellation drops this active pointer's queued presses; an ordinary lift keeps its tap. */
		WELTALLCORE_API bool Cancel(int32 Pointer);
		void Reset() { Fingers.clear(); Presses.clear(); }
		WELTALLCORE_API FPadState Held() const;
		/** Retains quick down/up events until one game sample, independently of visual held state. */
		WELTALLCORE_API FPadState TakeForTick();
		WELTALLCORE_API bool IsTouched(ETouchControl Control) const;

	private:
		struct FFinger { int32 Pointer; ETouchControl Control; FTouchPoint Point; };
		struct FPress { int32 Pointer; uint16 Buttons; };
		FPadState FingerHeld(const FFinger& Finger) const;
		void QueuePress(int32 Pointer, uint16 PressedButtons);
		FTouchLayout Layout;
		std::vector<FFinger> Fingers;
		std::vector<FPress> Presses;
	};

	/** Input sources are combined, never injected as key-up events into one another. */
	constexpr FPadState MergePads(FPadState Hardware, FPadState Touch)
	{
		// A touch direction is a digital press, even when the stick holds that direction too.
		Hardware.StickButtons &= static_cast<uint16>(~Touch.Buttons);
		Hardware.Buttons |= Touch.Buttons;
		return Hardware;
	}
}
