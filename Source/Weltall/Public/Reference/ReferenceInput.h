#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Screens/GameLoopScreen.h"

/** Identical normal input for editor and on-device Development references. Never staged game state. */
namespace WeltallReference
{
	/** A step of the pad script: the buttons held, and how far the view's axes are pushed (-1 to 1). */
	struct FPadStep
	{
		Weltall::FPadState Held;
		float Turn = 0.0f;
		float Tilt = 0.0f;
		float Zoom = 0.0f;

		/** What the step does to the view over Seconds, pushed as a stick is. */
		Weltall::FFieldViewInput ViewInput(double Seconds) const
		{
			const float Time = static_cast<float>(Seconds);
			return {Turn * Weltall::FFieldViewInput::TurnPerSecond * Time, Tilt * Weltall::FFieldViewInput::TiltPerSecond * Time,
				Zoom * Weltall::FFieldViewInput::ZoomPerSecond * Time, false};
		}
	};


	/**
	 * -Reader=Base,PerGlyph: the confirm button pressed as someone reading would press it. Once a message waits for it
	 * (WaitingMessageGlyphs), the button goes down after Base seconds and PerGlyph more for each glyph the page shows,
	 * and stays down a moment; whatever the pad script holds is held with it.
	 */
	struct WELTALL_API FRenderReader
	{
		bool bReads = false;
		double Base = 0.0;
		double PerGlyph = 0.0;
		/** How long the page on screen has waited, and how long the button has been down. */
		double Waited = 0.0;
		double Held = -1.0;

		static constexpr double HoldSeconds = 0.1;

		bool Parse(const FString& Params);

		/** Whether the reader holds the confirm button over the next Seconds of the game. */
		bool Holds(const FGameLoopScreen& Game, double Seconds)
		{
			if (Held >= 0.0)
			{
				Held += Seconds;
				if (Held < HoldSeconds)
				{
					return true;
				}
				Held = -1.0;
				Waited = 0.0;
				return false;
			}
			const TOptional<int32> Glyphs = Game.WaitingMessageGlyphs();
			if (!Glyphs.IsSet())
			{
				Waited = 0.0;
				return false;
			}
			Waited += Seconds;
			if (Waited < Base + PerGlyph * Glyphs.GetValue())
			{
				return false;
			}
			Held = 0.0;
			return true;
		}
	};


	WELTALL_API bool ParsePadScript(const FString& Params, TArray<FPadStep>& Script, double& Step);
}

#endif
