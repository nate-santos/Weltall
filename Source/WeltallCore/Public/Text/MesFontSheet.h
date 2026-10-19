#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Text/MesCharacters.h"
#include "Text/MesFont.h"
#include "Text/MesText.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Weltall
{
	/** A picture of four bytes a pixel (red, green, blue, alpha), its rows top down. */
	struct FRgbaPicture
	{
		int32 Width = 0;
		int32 Height = 0;
		std::vector<uint8> Pixels;
	};

	/** A glyph of the font's sheet: its code, its cell, and the character it stands for (none for a mark's). */
	struct FMesSheetGlyph
	{
		FMesCode Code;
		int32 Cell = 0;
		std::string Character;
	};

	/**
	 * The font as a sheet to edit (Assets/font, Docs/Modding.md): each glyph's 12 x 11 pixels in a cell of a grid 16 cells
	 * wide, white where the glyph draws, in the codes' order: 10h to FDh, FE 00 to FE 3C, then the box glyph FF FF. Its
	 * table has a line for each cell: the code, the cell's number, its width as the game advances it (8 or 12 pixels), the
	 * character it stands for, and how many of the game's messages type it.
	 */
	namespace MesFontSheet
	{
		inline constexpr int32 Columns = 16;
		inline constexpr int32 CellWidth = FMesFont::GlyphColumns;
		inline constexpr int32 CellHeight = FMesFont::GlyphRows;
		/** The most a player's sheet may magnify its cells. */
		inline constexpr int32 MostScale = 16;
		/** The cells' rows. */
		WELTALLCORE_API int32 Rows();

		/** Every glyph of the USA font in the sheet's order, each with the character Characters gives it. */
		WELTALLCORE_API std::vector<FMesSheetGlyph> Glyphs(const FMesCharacters& Characters);

		/** The sheet's picture of Font's glyphs at their size. */
		WELTALLCORE_API FRgbaPicture Picture(const FMesFont& Font);

		/** The sheet's table, Uses giving each glyph's count of messages (by its code's lead << 8 | code). */
		WELTALLCORE_API std::string Table(const FMesFont& Font, std::span<const FMesSheetGlyph> Glyphs, const std::unordered_map<uint16, int32>& Uses);
	}

	/** A sheet of the player's (Assets/font): the glyphs its picture draws and the characters its table names. */
	class FMesFontSheet
	{
	public:
		using FReport = std::function<void(const std::string&)>;

		/**
		 * The sheet of a table (empty: the export's own, every cell where the font's sheet has it) and a picture of the
		 * grid at one to MostScale times its size (none: the font's glyphs stay its own). A glyph's pixel is drawn where
		 * its cell's pixels are on average at least half opaque. Problems are reported; a sheet that cannot be read is an error.
		 */
		WELTALLCORE_API static TResult<std::shared_ptr<const FMesFontSheet>> Read(std::string_view Table, const FRgbaPicture* Picture, FReport Report);

		/** The glyphs the picture draws, as the font's rows. */
		const std::vector<std::pair<FMesCode, FMesFont::FGlyphRows>>& Glyphs() const { return Drawn; }

		/** The font's characters with the table's own in place. */
		const FMesCharacters& Characters() const { return Typed; }

		/** The glyphs whose width in the table is not the game's own: 8 pixels (2 units) for one above 60h, or 12 (3) below. */
		const FMesGlyphWidths& Widths() const { return OwnWidths; }

		/** How many times its cells are magnified; 0 without a picture. */
		int32 Scale() const { return PictureScale; }

		/** Font with the sheet's glyphs drawn in it, and their widths. */
		WELTALLCORE_API void DrawInto(FMesFont& Font) const;

	private:
		std::vector<std::pair<FMesCode, FMesFont::FGlyphRows>> Drawn;
		FMesCharacters Typed = FMesCharacters::Font();
		FMesGlyphWidths OwnWidths;
		int32 PictureScale = 0;
	};
}
