#include "Foundation/Crc32.h"
#include "Graphics/BeetleTextureNames.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <vector>

using namespace Weltall;

TEST_CASE("A Beetle PSX HW texture name is two CRC-32s in hex, joined by a dash", "[Graphics]")
{
	CHECK(BeetleTextureFileName({0x49B52D64, 0x87A54895}) == "49b52d64-87a54895");
	CHECK(BeetleTextureFileName({0xB2E0CE6, 0}) == "b2e0ce6-0");
	const auto Read = BeetleTextureNameOf("49B52D64-87a54895");
	REQUIRE(Read);
	CHECK(*Read == FBeetleTextureName{0x49B52D64, 0x87A54895});
	CHECK(BeetleTextureNameOf("0-0") == FBeetleTextureName{0, 0});
	// Never one of Weltall's own names, which end in sixteen hex digits.
	CHECK_FALSE(BeetleTextureNameOf("face-0123456789abcdef"));
	CHECK_FALSE(BeetleTextureNameOf("49b52d64"));
	CHECK_FALSE(BeetleTextureNameOf("49b52d64-87a548951"));
	CHECK_FALSE(BeetleTextureNameOf("49b52d64-87a5489g"));
	CHECK_FALSE(BeetleTextureNameOf("-87a54895"));
}

TEST_CASE("A region's Beetle parts are the uploads under its picture, through its view's palette", "[Graphics]")
{
	FVram Vram;
	// A 4-bit picture of 8 x 4 units (32 x 4 texels) at (64, 0), its first two units' texels naming 1 to 3; a CLUT beside it.
	FVramImage Picture{{64, 0, 8, 4}, std::vector<uint16>(32, 0x3211)};
	Vram.Upload(Picture);
	FVramImage Clut{{0, 480, 16, 1}, {}};
	for (uint16 Index = 0; Index < 16; ++Index)
	{
		Clut.Units.push_back(static_cast<uint16>(0x100 + Index));
	}
	Vram.Upload(Clut);
	// Another upload over its last two units' columns.
	Vram.Upload({{70, 0, 2, 4}, std::vector<uint16>(8, 0x7777)});

	FTexturePage Page;
	Page.X = 64;
	Page.Depth = ETextureDepth::Indexed4;
	const uint32 View = PackTextureView(Page, {0, 480});
	// Texels 4 to 27 of the page's first rows: from the picture's second unit into the later upload's columns.
	const std::vector<FBeetleTexturePart> Parts = BeetleTextureParts(Vram, {View, 4, 1, 24, 2});
	REQUIRE(Parts.size() == 2);
	const uint32 Palette = Crc32OfUnits(Clut.Units);
	const FBeetleTexturePart& First = Parts[0];
	CHECK(First.Upload == Crc32OfUnits(Picture.Units));
	CHECK(First.Palette == Palette);
	CHECK(First.ReducedPalette == Crc32OfUnits(std::span(Clut.Units).subspan(1, 3)));
	CHECK((First.UploadUnitsWide == 8 && First.UploadUnitsHigh == 4));
	CHECK((First.FromU == 4 && First.FromV == 1));
	CHECK((First.U == 0 && First.V == 0 && First.Width == 20 && First.Height == 2));
	const FBeetleTexturePart& Second = Parts[1];
	CHECK(Second.Upload == Crc32OfUnits(std::vector<uint16>(8, 0x7777)));
	CHECK((Second.UploadUnitsWide == 2 && Second.FromU == 0 && Second.FromV == 1));
	CHECK((Second.U == 20 && Second.V == 0 && Second.Width == 4 && Second.Height == 2));

	// Through no CLUT, a direct page's palette is 0.
	Page.Depth = ETextureDepth::Direct15;
	const std::vector<FBeetleTexturePart> Direct = BeetleTextureParts(Vram, {PackTextureView(Page, {}), 0, 0, 4, 1});
	REQUIRE(Direct.size() == 1);
	CHECK((Direct[0].Palette == 0 && Direct[0].ReducedPalette == 0));
}
