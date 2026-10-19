#include "Graphics/Texture.h"
#include "Graphics/Vram.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

namespace
{
	FVramImage Image(int16 X, int16 Y, int16 Width, int16 Height, std::vector<uint16> Units)
	{
		return {{X, Y, Width, Height}, std::move(Units)};
	}
}

TEST_CASE("VRAM uploads wrap at the edges like the GPU's transfers", "[Graphics]")
{
	FVram Vram;
	Vram.Upload(Image(1023, 511, 2, 2, {1, 2, 3, 4}));
	CHECK(Vram.At(1023, 511) == 1);
	CHECK(Vram.At(0, 511) == 2);
	CHECK(Vram.At(1023, 0) == 3);
	CHECK(Vram.At(0, 0) == 4);
	CHECK(Vram.At(1024, 512) == 4);
	CHECK(Vram.At(1, 1) == 0);
}

TEST_CASE("VRAM counts its uploads, so what was decoded from it can tell it is out of date", "[Graphics]")
{
	FVram Vram;
	CHECK(Vram.Revision() == 0);
	Vram.Upload(Image(0, 0, 1, 1, {7}));
	Vram.Upload(Image(0, 0, 1, 1, {7}));
	CHECK(Vram.Revision() == 2);
	const FVram Copy = Vram;
	CHECK(Copy.Revision() == 2);
	// A copy is of the same lineage, so its revisions compare with the original's; another VRAM's do not.
	CHECK(Copy.Lineage() == Vram.Lineage());
	CHECK(FVram{}.Lineage() != Vram.Lineage());
}

TEST_CASE("A colour the GPU fills the screen with meets a texel of the same 15-bit colour", "[Graphics]")
{
	// Shevat's sky, cleared to (104, 144, 248), is (13, 18, 31) in VRAM, as are the texels around its clouds.
	CHECK(FilledChannel(104) == WidenChannel(13));
	CHECK(FilledChannel(144) == WidenChannel(18));
	CHECK(FilledChannel(248) == WidenChannel(31));
	CHECK(FilledChannel(248) == 255);
	// The low three bits are dropped: a fill a little brighter than a 15-bit colour still shows as it.
	CHECK(FilledChannel(255) == 255);
	CHECK(FilledChannel(150) == WidenChannel(18));
	CHECK(FilledChannel(7) == 0);
}

TEST_CASE("Texture page and CLUT attributes decode to VRAM positions", "[Graphics]")
{
	// 8-bit, blend mode 1, second row of pages, fifth page across.
	const FTexturePage Page = FTexturePage::FromAttribute(1 << 7 | 1 << 5 | 1 << 4 | 5);
	CHECK(Page.X == 320);
	CHECK(Page.Y == 256);
	CHECK(Page.Depth == ETextureDepth::Indexed8);
	CHECK(Page.Blend == EBlendMode::Add);
	CHECK(FTexturePage::FromAttribute(2 << 7).Depth == ETextureDepth::Direct15);
	CHECK(FTexturePage::FromAttribute(3 << 7).Depth == ETextureDepth::Direct15);
	CHECK(FTexturePage::FromAttribute(3 << 5).Blend == EBlendMode::AddQuarter);

	const FClutPosition Clut = FClutPosition::FromAttribute(static_cast<uint16>(500 << 6 | 3));
	CHECK(Clut.X == 48);
	CHECK(Clut.Y == 500);
}

TEST_CASE("The port's rows of VRAM lie below the console's, which never wrap into them", "[Graphics]")
{
	FVram Vram;
	Vram.UploadToPortRows(Image(1023, FVram::Height - 1, 2, 3, {1, 2, 3, 4, 5, 6}));
	// The row above the port's is left out; columns wrap.
	CHECK(Vram.TexelAt(1023, FVram::Height) == 3);
	CHECK(Vram.TexelAt(0, FVram::Height) == 4);
	CHECK(Vram.TexelAt(0, FVram::Height + 1) == 6);
	CHECK(Vram.At(1023, FVram::Height - 1) == 0);
	CHECK(Vram.Revision() == 1);
	// The console's own reads and uploads wrap within its rows, past the port's.
	CHECK(Vram.At(1023, FVram::Height) == 0);
	Vram.Upload(Image(5, FVram::Height - 1, 1, 2, {7, 8}));
	CHECK(Vram.At(5, 0) == 8);
	CHECK(Vram.TexelAt(5, FVram::Height) == 0);
	// Nothing lies past the port's rows; the console's tiles do not count theirs.
	Vram.UploadToPortRows(Image(9, FVram::Rows - 1, 1, 2, {9, 10}));
	CHECK(Vram.TexelAt(9, FVram::Rows - 1) == 9);
	CHECK(Vram.TexelAt(9, FVram::Rows) == 0);
	CHECK(Vram.AllUnits().size() == static_cast<std::size_t>(FVram::Width) * FVram::Rows);
	const FVramTiles Changed = Vram.TilesChangedSince(1);
	CHECK(Changed[0] == 1);
	CHECK(Changed[FVram::TilesDown - 1] == 1);
}

TEST_CASE("A page's or CLUT's high y bit reaches the port's rows only as a 2 MB GPU reads it", "[Graphics]")
{
	// 4-bit, the first row of pages, the second page across, bit 11 set; a CLUT at (32, 513).
	const uint16 PageAttribute = 1 << 11 | 1;
	const uint16 ClutAttribute = static_cast<uint16>(513 << 6 | 2);
	CHECK(FTexturePage::FromAttribute(PageAttribute).Y == 0);
	CHECK(FTexturePage::FromPortAttribute(PageAttribute).Y == FVram::Height);
	CHECK(FTexturePage::FromPortAttribute(PageAttribute).X == 64);
	CHECK(FTexturePage::FromPortAttribute(1 << 4).Y == 256);
	CHECK(FClutPosition::FromAttribute(ClutAttribute).Y == 1);
	CHECK(FClutPosition::FromPortAttribute(ClutAttribute).Y == 513);
	CHECK(FClutPosition::FromPortAttribute(ClutAttribute).X == 32);

	FVram Vram;
	Vram.UploadToPortRows(Image(64, FVram::Height + 3, 1, 1, {0x0021}));
	Vram.UploadToPortRows(Image(32, 513, 3, 1, {0, 0x1234, 0x7FFF}));
	const FTexturePage Page = FTexturePage::FromPortAttribute(PageAttribute);
	const FClutPosition Clut = FClutPosition::FromPortAttribute(ClutAttribute);
	CHECK(FetchTexel(Vram, Page, Clut, 0, 3) == 0x1234);
	CHECK(FetchTexel(Vram, Page, Clut, 1, 3) == 0x7FFF);
	CHECK(FetchTexel(Vram, Page, Clut, 2, 3) == 0);
	// The same texel through the console's reading is row 3 of the console's first row of pages, empty.
	CHECK(FetchTexel(Vram, FTexturePage::FromAttribute(PageAttribute), FClutPosition::FromAttribute(ClutAttribute), 0, 3) == 0);
}

TEST_CASE("Indexed texels are looked up in their CLUT", "[Graphics]")
{
	FVram Vram;
	std::vector<uint16> Colors(256);
	for (uint16 Index = 0; Index < 256; ++Index)
	{
		Colors[Index] = static_cast<uint16>(0x1000 + Index);
	}
	Vram.Upload(Image(16, 480, 256, 1, Colors));
	Vram.Upload(Image(64, 0, 2, 1, {0x4321, 0x8765}));
	Vram.Upload(Image(128, 0, 2, 1, {0xBBAA, 0xDDCC}));
	Vram.Upload(Image(192, 5, 1, 1, {0x7C1F}));

	const FClutPosition Clut{16, 480};
	const FTexturePage Page4{64, 0, ETextureDepth::Indexed4, EBlendMode::Average};
	CHECK(FetchTexel(Vram, Page4, Clut, 0, 0) == 0x1001);
	CHECK(FetchTexel(Vram, Page4, Clut, 3, 0) == 0x1004);
	CHECK(FetchTexel(Vram, Page4, Clut, 7, 0) == 0x1008);

	const FTexturePage Page8{128, 0, ETextureDepth::Indexed8, EBlendMode::Average};
	CHECK(FetchTexel(Vram, Page8, Clut, 0, 0) == 0x10AA);
	CHECK(FetchTexel(Vram, Page8, Clut, 1, 0) == 0x10BB);
	CHECK(FetchTexel(Vram, Page8, Clut, 3, 0) == 0x10DD);

	const FTexturePage Page15{192, 0, ETextureDepth::Direct15, EBlendMode::Average};
	CHECK(FetchTexel(Vram, Page15, Clut, 0, 5) == 0x7C1F);
}

TEST_CASE("Opaque decoding makes only texel 0 transparent and widens 5-bit channels", "[Graphics]")
{
	FVram Vram;
	Vram.Upload(Image(0, 0, 4, 1, {0x0000, 0x8000, 0x7FFF, 0x0421}));
	const FImageRgba8 Texture = DecodeOpaqueTexture(Vram, {0, 0, ETextureDepth::Direct15, EBlendMode::Average}, {});
	REQUIRE(Texture.Width == 256);
	REQUIRE(Texture.Height == 256);
	const auto Pixel = [&](int32 U) { return std::vector<uint8>(Texture.Pixels.begin() + U * 4, Texture.Pixels.begin() + U * 4 + 4); };
	CHECK(Pixel(0) == std::vector<uint8>{0, 0, 0, 0});
	CHECK(Pixel(1) == std::vector<uint8>{0, 0, 0, 255});
	CHECK(Pixel(2) == std::vector<uint8>{255, 255, 255, 255});
	CHECK(Pixel(3) == std::vector<uint8>{8, 8, 8, 255});
}

TEST_CASE("Decoding for semi-transparency marks the texels with bit 15 set", "[Graphics]")
{
	FVram Vram;
	Vram.Upload(Image(0, 0, 4, 1, {0x0000, 0x8000, 0x7FFF, 0xFC00}));
	const FImageRgba8 Texture = DecodeTexture(Vram, {0, 0, ETextureDepth::Direct15, EBlendMode::Add}, {});
	const auto Alpha = [&](int32 U) { return Texture.Pixels[static_cast<std::size_t>(U * 4 + 3)]; };
	CHECK(Alpha(0) == 0);
	CHECK(Alpha(1) == SemiTransparentTexelAlpha);
	CHECK(Alpha(2) == 255);
	CHECK(Alpha(3) == SemiTransparentTexelAlpha);
	CHECK(Texture.Pixels[3 * 4 + 2] == 255);
}
