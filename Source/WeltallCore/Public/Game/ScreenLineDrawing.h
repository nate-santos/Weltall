#pragma once

#include "Foundation/Types.h"
#include "Graphics/Vram.h"
#include "Presentation/DrawList.h"
#include "Presentation/GameScreen.h"

#include <string>
#include <string_view>

namespace Weltall
{
	class FMesFont;

	/**
	 * A line in the game's message font that the port draws over the 320 x 224 game screen: white letters over their
	 * outlines, drawn with the GPU's blends so that they rise and fade with their strength (the outlines darken what is
	 * behind them, the letters lighten it to white). Its pictures are two 15-bit rows of pages at VRAM's corner, the
	 * line drawn into them the first time it is asked for.
	 */
	class FScreenLineDrawing
	{
	public:
		/** Line's width in pixels, as many letters as the screen's width holds, drawn into the pictures if it is new. */
		WELTALLCORE_API int32 Prepare(std::string_view Line, const FMesFont& Font);

		/** The prepared line at (X, Y) of the screen, at Strength (0 to 255), cut to Clip. */
		WELTALLCORE_API void Add(int32 X, int32 Y, int32 Strength, const FScreenRect& Clip, FDrawList& Out) const;

		const FVram& Pictures() const { return PictureVram; }

	private:
		FVram PictureVram;
		std::string Drawn;
		int32 TextWidth = 0;
	};
}
