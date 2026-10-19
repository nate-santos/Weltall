#include "Game/ChapterCardDrawing.h"

#include "Game/ChapterCard.h"
#include "Game/ChapterTitles.h"
#include "Text/MesFont.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/** The band: its rows on the screen, the rows over which each edge fades, and how dark its middle gets. */
		constexpr int16 BandTop = 92;
		constexpr int16 BandHeight = 40;
		constexpr int16 BandEdge = 12;
		constexpr int32 BandDarkness = 88;
		/** Far past any display's side, on the screen's scale. */
		constexpr int16 Beyond = 8192;
		constexpr FScreenRect WholeWidth{-Beyond, 0, 320 + 2 * Beyond, 224};

		FDrawColor Grey(int32 Level)
		{
			const uint8 Value = static_cast<uint8>(std::clamp(Level, 0, 255));
			return {Value, Value, Value};
		}

		/** A band's rows across the whole display, darkening by Top at its top and Bottom at its foot. */
		FDrawPrimitive Band(int16 Y, int16 Height, int32 Top, int32 Bottom)
		{
			FDrawPrimitive Rows = MakeGouraudRect({WholeWidth.X, Y, WholeWidth.Width, Height}, {Grey(Top), Grey(Top), Grey(Bottom), Grey(Bottom)});
			Rows.Blend = EDrawBlend::SemiTransparent;
			Rows.UntexturedBlend = EBlendMode::Subtract;
			Rows.Clip = WholeWidth;
			return Rows;
		}
	}

	std::string FChapterCardDrawing::Text(const FChapter& Chapter)
	{
		return "Chapter " + std::to_string(Chapter.Number) + ": " + Chapter.Title;
	}

	std::string FChapterCardDrawing::Text(const FChapter& Chapter, int32 Part, std::string_view Subtitle)
	{
		return "Chapter " + std::to_string(Chapter.Number) + static_cast<char>('a' + Part - 1) + ": " + std::string(Subtitle);
	}

	void FChapterCardDrawing::Build(std::string_view Line, int32 Level, const FMesFont& Font, FDrawList& Out)
	{
		Out.Primitives.clear();
		if (Level <= 0)
		{
			return;
		}
		const int32 TextWidth = Words.Prepare(Line, Font);
		const int32 Dark = BandDarkness * Level / FChapterCard::FullLevel;
		Out.Primitives.push_back(Band(BandTop, BandEdge, 0, Dark));
		Out.Primitives.push_back(Band(BandTop + BandEdge, BandHeight - 2 * BandEdge, Dark, Dark));
		Out.Primitives.push_back(Band(BandTop + BandHeight - BandEdge, BandEdge, Dark, 0));
		const int32 X = (ChapterCardScreen.Width - (TextWidth - FMesFont::CellWidth)) / 2;
		const int32 Y = BandTop + (BandHeight - FMesFont::CellHeight) / 2;
		Words.Add(X, Y, 255 * Level / FChapterCard::FullLevel, WholeWidth, Out);
	}
}
