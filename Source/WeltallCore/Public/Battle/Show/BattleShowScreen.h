#pragma once

#include "Foundation/Types.h"
#include "Graphics/Gte.h"

#include <array>

namespace Weltall
{
	/**
	 * Battle's full-screen fade: one semi-transparent tile over everything but the HUD, blended by one of
	 * the GPU's four modes, whose colour moves from where it was to a target over twice the steps asked
	 * for. A fade to black ends when it gets there (subtracting black changes nothing); any other stays
	 * on the screen. Its task steps with the other tasks, and once as it starts.
	 */
	struct FBattleFade
	{
		bool bOn = false;
		/** The GPU blend mode it draws with (0 half, 1 add, 2 subtract, 3 add a quarter). */
		uint8 Mode = 0;
		std::array<uint8, 3> Target{};
		std::array<uint8, 3> From{};
		/** The colour drawn. */
		std::array<uint8, 3> Shown{};
		int32 Total = 0;
		int32 Remaining = 0;
		/** System commands 5A and 59: new fades are refused, or allowed again. */
		bool bBlocked = false;
		/** The last fade started was an effect's (its script's command CAh), not the battle's own. */
		bool bFromEffect = false;

		/** A fade's start: a new target from the colour shown now; nothing while fades are blocked. */
		WELTALLCORE_API void Start(int32 Steps, int32 BlendMode, uint8 Red, uint8 Green, uint8 Blue, bool bEffect = false);
		/** One step of its task. */
		WELTALLCORE_API void Step();
		/** The mode a script's fade takes when it names none: the current fade's, or 1 without one. */
		uint8 ModeOrAdd() const { return bOn ? Mode : 1; }
	};

	/**
	 * The camera shake: an amplitude moving from where it was to a target over twice the steps asked
	 * for, the view's offset that amplitude with its sign flipping every two steps; its task ends once
	 * the amplitude has reached zero.
	 */
	struct FBattleShake
	{
		bool bOn = false;
		FGteVector Amplitude{};
		FGteVector From{};
		FGteVector Target{};
		/** The steps run, whose second bit flips the offset's sign. */
		uint32 Count = 0;
		int32 Remaining = 0;
		int32 Total = 0;
		/** What the view's translation is offset by: the last step's, kept when the shake ends. */
		FGteVector Offset{};

		/** A shake's start: toward NewTarget over Steps. */
		WELTALLCORE_API void Start(const FGteVector& NewTarget, int32 Steps);
		/** One step of its task. */
		WELTALLCORE_API void Step();
	};

	/**
	 * The stage's four tint layers, which recolour its palettes, the floor dome, the panoramas and the
	 * clear colours: each on or off, a mode (0 add the colour, 1 half of it, 2 a quarter, 3 its grey,
	 * 4 black), a level 0-32 from the layer's colour to the original, and a colour in eighths.
	 */
	struct FBattleTintLayers
	{
		static constexpr int32 LayerCount = 4;

		struct FLayer
		{
			uint8 bOn = 0;
			uint8 Mode = 0;
			uint8 Level = 0;
			std::array<int8, 3> Color{};
		};

		std::array<FLayer, LayerCount> Layers{};
		/** A layer changed since the stage last applied them. */
		bool bChanged = false;

		/** A negative mode switches the layer off; layers past the four are ignored. */
		WELTALLCORE_API void Set(int32 Layer, int32 Mode, int32 Level, int32 Red, int32 Green, int32 Blue);
		/** Switches every layer off, as the layers are made for a fade. */
		WELTALLCORE_API void SwitchOff();
		/** The tint: Color seen through the layer; the result is written back into Color too. */
		WELTALLCORE_API std::array<uint8, 3> Tint(int32 Layer, std::array<uint8, 3>& Color) const;
	};

	/**
	 * System command 3Ah's fade of tint layer 0: its level moves from where it was to a target over twice
	 * the steps asked for; each frame's drawing sets layer 0 when the level changed (32 less it, the
	 * command's mode and colour); its task ends once the level has come back to 0.
	 */
	struct FBattleTintFade
	{
		bool bOn = false;
		int16 From = 0;
		int16 Target = 0;
		/** The level the drawing last gave layer 0. */
		int16 Drawn = 0;
		int16 Level = 0;
		int16 Mode = 0;
		std::array<int16, 3> Color{};
		int32 Total = 0;
		int32 Remaining = 0;

		/**
		 * A fade of the tint; whether this made a new task, which switches every layer off (and the
		 * stage keeps its palettes aside until the task ends).
		 */
		WELTALLCORE_API bool Start(FBattleTintLayers& Layers, int16 NewTarget, int32 Steps, int16 NewMode, int16 Red, int16 Green, int16 Blue);
		/** One step of its task; false when the task ended. */
		WELTALLCORE_API bool Step();
		/** Its task's drawing: tint layer 0 follows the level when it changed. */
		WELTALLCORE_API void Draw(FBattleTintLayers& Layers);
	};
}
