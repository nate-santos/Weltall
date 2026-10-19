#include "Support/GameDiscs.h"
#include "Support/GameText.h"
#include "Text/MesFontSheet.h"

#include <catch_amalgamated.hpp>

#include <algorithm>

using namespace Weltall;

namespace
{
	/** Picture at Scale times its size, each pixel a square of its own. */
	FRgbaPicture Magnified(const FRgbaPicture& Picture, int32 Scale)
	{
		FRgbaPicture Large{Picture.Width * Scale, Picture.Height * Scale, {}};
		Large.Pixels.resize(static_cast<std::size_t>(Large.Width * Large.Height * 4));
		for (int32 Y = 0; Y < Large.Height; ++Y)
		{
			for (int32 X = 0; X < Large.Width; ++X)
			{
				const std::size_t From = static_cast<std::size_t>((Y / Scale) * Picture.Width + X / Scale) * 4;
				std::copy_n(Picture.Pixels.begin() + static_cast<std::ptrdiff_t>(From), 4, Large.Pixels.begin() + static_cast<std::ptrdiff_t>((Y * Large.Width + X) * 4));
			}
		}
		return Large;
	}

	void SetPixel(FRgbaPicture& Picture, int32 X, int32 Y, uint8 Alpha)
	{
		std::fill_n(Picture.Pixels.begin() + static_cast<std::ptrdiff_t>((Y * Picture.Width + X) * 4), 4, Alpha);
	}
}

TEST_CASE("The font's sheet gives back every glyph of the font, at its size and magnified", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Text = Testing::LoadMesResident(*Disc);
	const FRgbaPicture Picture = MesFontSheet::Picture(Text.Font);
	CHECK(Picture.Width == 16 * 12);
	CHECK(Picture.Height == 19 * 11);
	const std::vector<FMesSheetGlyph> Glyphs = MesFontSheet::Glyphs(FMesCharacters::Font());
	REQUIRE(Glyphs.size() == 300);
	const std::string Table = MesFontSheet::Table(Text.Font, Glyphs, {});
	for (const int32 Scale : {1, 3, 16})
	{
		INFO("scale " << Scale);
		const FRgbaPicture Shown = Magnified(Picture, Scale);
		const auto Sheet = FMesFontSheet::Read(Table, &Shown, nullptr);
		REQUIRE(Sheet);
		CHECK((*Sheet)->Scale() == Scale);
		REQUIRE((*Sheet)->Glyphs().size() == Glyphs.size());
		for (const auto& [Code, Rows] : (*Sheet)->Glyphs())
		{
			CHECK(Text.Font.Rows(Code) == Rows);
		}
	}
	const auto Plain = FMesFontSheet::Read({}, &Picture, nullptr);
	REQUIRE(Plain);
	CHECK((*Plain)->Glyphs().size() == Glyphs.size());
	CHECK((*Plain)->Characters().CodeAt("…")->Code == FMesCode{0, 0x8E});
}

TEST_CASE("Every narrow glyph of the font keeps its outline inside its 8 pixels", "[Text][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Text = Testing::LoadMesResident(*Disc);
	for (uint8 Code = 0x10; Code < 0x61; ++Code)
	{
		INFO("code " << int(Code));
		const auto Rows = Text.Font.Rows({0, Code});
		REQUIRE(Rows);
		CHECK(std::all_of(Rows->begin(), Rows->end(), [](uint16 Row) { return (Row & 0x03FF) == 0; }));
	}
}

TEST_CASE("A sheet of the player's redraws its glyphs and names their characters", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FMesResident Text = Testing::LoadMesResident(*Disc);
	FRgbaPicture Picture = MesFontSheet::Picture(Text.Font);
	// ぁ (cell 131) becomes é: its cell cleared but for an accent over a dot, its table line naming the new character.
	const int32 Left = 131 % 16 * 12;
	const int32 Top = 131 / 16 * 11;
	for (int32 Y = 0; Y < 11; ++Y)
	{
		for (int32 X = 0; X < 12; ++X)
		{
			SetPixel(Picture, Left + X, Top + Y, 0);
		}
	}
	SetPixel(Picture, Left + 4, Top + 1, 255);
	SetPixel(Picture, Left + 3, Top + 6, 200);
	// A half transparent pixel stays clear; an "A" drawn into its eighth column spills past its advance.
	SetPixel(Picture, Left + 5, Top + 6, 100);
	const int32 CellOfA = 0x20 - 0x10;
	SetPixel(Picture, CellOfA % 16 * 12 + 7, CellOfA / 16 * 11 + 3, 255);
	std::vector<std::string> Reports;
	// é is made as narrow as the letters; a width neither 8 nor 12 is refused.
	const auto Sheet = FMesFontSheet::Read("code\tcell\twidth\tcharacter\tuses\n93\t131\t8\t\xC3\xA9\t0\n20\t16\t8\tA\t1\nZZ\t3\nFE3D\t300\n94\t132\t10\t\t0\n",
		&Picture, [&Reports](const std::string& Line) { Reports.push_back(Line); });
	REQUIRE(Sheet);
	REQUIRE((*Sheet)->Glyphs().size() == 3);
	CHECK((*Sheet)->Characters().CodeAt("\xC3\xA9")->Code == FMesCode{0, 0x93});
	CHECK_FALSE((*Sheet)->Characters().CodeAt("ぁ"));
	CHECK((*Sheet)->Widths() == FMesGlyphWidths{{0x93, 2}});
	(*Sheet)->DrawInto(Text.Font);
	const FMesFont::FGlyphRows Accent = {0, 0x0800, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0};
	CHECK(Text.Font.Rows({0, 0x93}) == Accent);
	CHECK(Text.Font.Cell({0, 0x93})[2 * 12 + 5] == 1);
	CHECK(Text.Font.WidthUnits({0, 0x93}) == 2);
	CHECK(Text.Font.WidthUnits({0, 0x94}) == 3);
	CHECK(Text.Font.WidthUnits({0, 0x20}) == 2);
	REQUIRE(Reports.size() == 4);
	CHECK(Reports[0].starts_with("font.tsv line 4: a cell is a glyph's code"));
	CHECK(Reports[1].starts_with("font.tsv line 5: a cell is a glyph's code"));
	CHECK(Reports[2] == "font.tsv line 6: a glyph is 8 or 12 pixels wide: 94 keeps its own width");
	CHECK(Reports[3] == "font.png: glyph 20 draws past its first 6 columns: the game advances it 8 pixels, and the next glyph cuts its outline's right edge");
	const FRgbaPicture Wrong{100, 100, std::vector<uint8>(100 * 100 * 4)};
	CHECK_FALSE(FMesFontSheet::Read({}, &Wrong, nullptr));
}
