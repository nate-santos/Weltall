#include "Game/TitleMarkDrawing.h"
#include "Support/GameDiscs.h"
#include "Support/GameText.h"
#include "Text/MesFont.h"
#include "Text/MesGlyphs.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

TEST_CASE("The title's mark is as bright as the title's picture: its fades, the menu's shade, its backdrop shown", "[Game]")
{
	CHECK(FTitleMarkDrawing::Text("0.8.1") == "Weltall 0.8.1");
	FFieldRenderState Render;
	CHECK(FTitleMarkDrawing::Strength(Render, false) == 255);

	// A fade toward black takes as much from the mark; a brightening one leaves it.
	Render.Fades[0].bActive = true;
	Render.Fades[0].BlendMode = 2;
	Render.Fades[0].Color = {96 << 8, 128 << 8, 64 << 8};
	CHECK(FTitleMarkDrawing::Strength(Render, false) == 127);
	Render.Fades[0].BlendMode = 1;
	CHECK(FTitleMarkDrawing::Strength(Render, false) == 255);
	Render.Fades[0].bActive = false;

	// The menu's shade darkens the field's own picture; the picture under an open menu is shaded by the menu itself.
	Render.MenuShade.bShown = true;
	Render.MenuShade.Level = 31;
	CHECK(FTitleMarkDrawing::Strength(Render, false) == 255 - 31 * FFieldMenuShade::GreyPerLevel);
	CHECK(FTitleMarkDrawing::Strength(Render, true) == 255);
	Render.MenuShade.Level = 62;
	CHECK(FTitleMarkDrawing::Strength(Render, false) == 7);

	// Once the scripts keep the screen dark the backdrop, the title's picture, is hidden, and the mark with it.
	Render.MenuShade.bShown = false;
	Render.Panorama.bHidden = true;
	CHECK(FTitleMarkDrawing::Strength(Render, false) == 0);
}

TEST_CASE("The title's mark ends under the logo's last letter, below the logo", "[Game][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FTitleMarkDrawing Drawing;
	FDrawList List;
	Drawing.Build("0.8.1", 0, Resident.Font, List);
	CHECK(List.Primitives.empty());
	Drawing.Build("0.8.1", 255, Resident.Font, List);
	REQUIRE(List.Primitives.size() == 2);
	const FDrawPrimitive& Letters = List.Primitives[1];
	REQUIRE(Letters.Texture);
	CHECK(Letters.Texture->Page.Blend == EBlendMode::Add);
	// Drawn as their texels are at full strength, not doubled: the mark's light follows the picture's from there down.
	CHECK(Letters.Corners[0].Color.R == 127);
	CHECK(Letters.Corners[0].Y == Catch::Approx(134.0f));
	// The line's advance ends a few columns past the logo's right edge (column 310); its last letter's ink on it.
	int32 Advance = 0;
	for (const char Character : FTitleMarkDrawing::Text("0.8.1"))
	{
		Advance += Resident.Font.WidthUnits(*MesCodeOfCharacter(Character)) * 4;
	}
	const float Left = Letters.Corners[0].X;
	CHECK(Left + static_cast<float>(Advance) == Catch::Approx(314.0f));
	// A longer version moves the mark so that it still ends there.
	Drawing.Build("0.10.12", 255, Resident.Font, List);
	CHECK(List.Primitives[1].Corners[0].X < Left);
}
