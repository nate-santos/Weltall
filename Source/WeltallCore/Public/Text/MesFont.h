#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <array>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Weltall
{
	/** A glyph as text stores it: one byte (lead 0), or a lead byte (FEh, FFh) and a second byte. */
	struct FMesCode
	{
		uint8 Lead = 0;
		uint8 Code = 0;

		bool operator==(const FMesCode&) const = default;
	};

	/**
	 * The game's message font: glyphs of 11 rows of 12 pixels, and the box glyph the kernel carries
	 * for codes the font lacks (FF FF). The kernel draws a glyph into a 12 x 13 cell of 2-bit values:
	 * 1 for its body, moved one pixel right and down, 2 for an outline around the body (3 where a
	 * body pixel also borders another), clipped to the cell.
	 */
	class FMesFont
	{
	public:
		static constexpr int32 CellWidth = 12;
		static constexpr int32 CellHeight = 13;
		static constexpr std::size_t GlyphBytes = 22;
		static constexpr int32 GlyphColumns = 12;
		static constexpr int32 GlyphRows = 11;
		using FCell = std::array<uint8, CellWidth * CellHeight>;
		/** A glyph's pixels: a row each, its leftmost pixel in bit 15 (bits 3 to 0 are never drawn). */
		using FGlyphRows = std::array<uint16, GlyphRows>;

		/** The font as its file unpacks (directory 1, file 6), and the executable's box glyph. */
		WELTALLCORE_API static TResult<FMesFont> Parse(std::span<const uint8> Font, std::span<const uint8> BoxGlyph);

		/** The lead byte from which codes take a second byte. */
		uint8 LeadByte() const { return SecondPageLead; }

		/** How far the text advances after a glyph, in units of 4 pixels: 2 for the narrow ones, 3 for the rest. */
		WELTALLCORE_API int32 WidthUnits(FMesCode Code) const;

		/** Code's glyph advances Units from now on, whatever the game's rule gives it (a font sheet's width). */
		WELTALLCORE_API void SetWidthUnits(FMesCode Code, int32 Units);

		/** The glyph's cell; empty for a code the font does not hold. */
		WELTALLCORE_API FCell Cell(FMesCode Code) const;

		/** The glyph's pixels; none for a code the font does not hold. */
		WELTALLCORE_API std::optional<FGlyphRows> Rows(FMesCode Code) const;

		/** Draws Rows for Code's glyph from now on; false (changing nothing) for a code the font does not hold. */
		WELTALLCORE_API bool SetRows(FMesCode Code, const FGlyphRows& Rows);

	private:
		/** Where Code's glyph is in Glyphs, or the box; none for a code the font does not hold. */
		std::optional<std::span<const uint8>> GlyphOf(FMesCode Code) const;

		std::vector<uint8> Glyphs;
		std::array<uint8, GlyphBytes> Box{};
		uint8 SecondPageLead = 0;
		uint32 SecondPageOffset = 0;
		uint32 NarrowCount = 0;
		uint32 SecondPageNarrowCount = 0;
		uint32 FirstCode = 0;
		/** The glyphs a font sheet gives widths of their own, by their code's lead << 8 | code. */
		std::unordered_map<uint16, int32> OwnWidths;
	};
}
