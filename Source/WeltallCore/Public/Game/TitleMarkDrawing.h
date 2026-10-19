#pragma once

#include "Field/FieldRenderState.h"
#include "Foundation/Types.h"
#include "Game/ScreenLineDrawing.h"

#include <string>
#include <string_view>

namespace Weltall
{
	class FMesFont;

	/** The port's name and version on the title screen, "Weltall 0.8.1", drawn over the title's picture and its menu. */
	class FTitleMarkDrawing
	{
	public:
		WELTALLCORE_API static std::string Text(std::string_view Version);

		/**
		 * The mark's strength (0 to 255) over the title's field as Render draws it: as bright as its fades leave the picture,
		 * and the menu's shade where the picture is not the one under an open menu (which shades it itself); none once the
		 * title's picture, its backdrop, is hidden.
		 */
		WELTALLCORE_API static int32 Strength(const FFieldRenderState& Render, bool bUnderMenu);

		/** The mark of Version at Level (0 to 255) as a list over the 320 x 224 game screen, its textures in Pictures(). */
		WELTALLCORE_API void Build(std::string_view Version, int32 Level, const FMesFont& Font, FDrawList& Out);

		const FVram& Pictures() const { return Words.Pictures(); }

	private:
		FScreenLineDrawing Words;
	};
}
