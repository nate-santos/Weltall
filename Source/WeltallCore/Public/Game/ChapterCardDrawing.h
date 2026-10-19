#pragma once

#include "Foundation/Types.h"
#include "Game/ScreenLineDrawing.h"
#include "Graphics/Vram.h"
#include "Presentation/DrawList.h"
#include "Presentation/GameScreen.h"

#include <string>
#include <string_view>

namespace Weltall
{
	class FMesFont;
	struct FChapter;

	/** The game screen a chapter's card is laid out on. */
	inline constexpr FScreenSize ChapterCardScreen{320, 224};

	/**
	 * A chapter's title card as the display draws it over the field: a line in the game's message font, "Chapter N:
	 * Title" or a subtitle's "Chapter Na: Subtitle", in the middle of a band across the whole display, drawn with the
	 * GPU's own blends so that it rises and fades with the card's level: the band and the letters' outlines darken what
	 * is behind them, the letters lighten it to white.
	 */
	class FChapterCardDrawing
	{
	public:
		/** A chapter's line: "Chapter 13: Pirate's Lair". */
		WELTALLCORE_API static std::string Text(const FChapter& Chapter);
		/** The line of the subtitle that is Part (from 1) of those inside Chapter: "Chapter 57a: Secret Battleship". */
		WELTALLCORE_API static std::string Text(const FChapter& Chapter, int32 Part, std::string_view Subtitle);

		/**
		 * Line's card at Level (FChapterCard's) as a list over ChapterCardScreen, its textures in Pictures(), where the
		 * line is drawn the first time it is asked for; as many letters as the screen's width holds.
		 */
		WELTALLCORE_API void Build(std::string_view Line, int32 Level, const FMesFont& Font, FDrawList& Out);

		const FVram& Pictures() const { return Words.Pictures(); }

	private:
		FScreenLineDrawing Words;
	};
}
