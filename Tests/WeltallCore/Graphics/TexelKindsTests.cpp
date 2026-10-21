#include "Graphics/TexelKinds.h"
#include "Graphics/TextureRegion.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

namespace
{
	constexpr uint16 Semi = 0x8000;
	constexpr uint16 Red = 0x001F;

	/** A 4-bit page at (0, 0) and its CLUT at (0, 256): Colors as the CLUT's sixteen. */
	struct FIndexedPage
	{
		FVram Vram;
		FTexturePage Page{0, 0, ETextureDepth::Indexed4, EBlendMode::Add};
		FClutPosition Clut{0, 256};

		explicit FIndexedPage(std::vector<uint16> Colors)
		{
			Colors.resize(16);
			Vram.Upload({{0, 256, 16, 1}, Colors});
		}

		/** Texel (U, V) given an index: four to a unit. */
		void SetIndex(int32 U, int32 V, uint16 Index)
		{
			const uint16 Unit = static_cast<uint16>((Vram.At(U / 4, V) & ~(0xF << (U % 4 * 4))) | Index << (U % 4 * 4));
			Vram.Upload({{static_cast<int16>(U / 4), static_cast<int16>(V), 1, 1}, {Unit}});
		}

		uint8 Kinds(FTexelRect Rect, int32 Margin = 0, bool bClutOnly = false) const
		{
			return TexelKindsWithin(Vram, Page, Clut, Rect, Margin, bClutOnly);
		}
	};
}

TEST_CASE("A texel's colour gives its kind as the GPU draws it", "[Graphics]")
{
	CHECK(TexelKindOf(0) == 0);
	CHECK(TexelKindOf(Red) == TexelKinds::Plain);
	CHECK(TexelKindOf(Red | Semi) == TexelKinds::SemiTransparent);
	// Black with the flag set is not transparent: it blends.
	CHECK(TexelKindOf(Semi) == TexelKinds::SemiTransparent);
}

TEST_CASE("A CLUT of one kind of colour tells the kinds of every texel read through it", "[Graphics]")
{
	// Rain: transparent where there is no streak, every streak's colour blended.
	const FIndexedPage Rain({0, Semi | 0x7FFF, Semi | 0x3DEF, Semi | 0x2108});
	CHECK(Rain.Kinds({0, 0, 63, 63}) == TexelKinds::SemiTransparent);
	CHECK(Rain.Kinds({0, 0, 63, 63}, 0, true) == TexelKinds::SemiTransparent);
	const FIndexedPage Solid({0, Red, 0x03E0});
	CHECK(Solid.Kinds({0, 0, 255, 255}) == TexelKinds::Plain);
	const FIndexedPage Empty({});
	CHECK(Empty.Kinds({0, 0, 15, 15}) == 0);
}

TEST_CASE("A CLUT of both kinds tells the kinds by the indices the texels use", "[Graphics]")
{
	FIndexedPage Mixed({0, Red, Red | Semi});
	// Every texel is index 0 (transparent) until set.
	CHECK(Mixed.Kinds({0, 0, 7, 7}) == 0);
	Mixed.SetIndex(3, 3, 2);
	CHECK(Mixed.Kinds({0, 0, 7, 7}) == TexelKinds::SemiTransparent);
	Mixed.SetIndex(9, 3, 1);
	CHECK(Mixed.Kinds({0, 0, 7, 7}) == TexelKinds::SemiTransparent);
	// An upscaling may take an index from beside the texels: the margin reaches it.
	CHECK(Mixed.Kinds({2, 2, 7, 7}, 2) == TexelKinds::Any);
	CHECK(Mixed.Kinds({0, 0, 9, 7}) == TexelKinds::Any);
	// A margin past the page, or a window's wrapping texels, can tell only by the CLUT.
	CHECK(Mixed.Kinds({0, 0, 7, 7}, 1) == TexelKinds::Any);
	CHECK(Mixed.Kinds({2, 2, 4, 4}, 0, true) == TexelKinds::Any);
}

TEST_CASE("A 15-bit page's kinds are its texels' own", "[Graphics]")
{
	FVram Vram;
	const FTexturePage Page{64, 0, ETextureDepth::Direct15, EBlendMode::Average};
	Vram.Upload({{64 + 4, 4, 2, 1}, {Red | Semi, Semi}});
	CHECK(TexelKindsWithin(Vram, Page, {}, {0, 0, 3, 3}, 0) == 0);
	CHECK(TexelKindsWithin(Vram, Page, {}, {4, 4, 7, 7}, 0) == TexelKinds::SemiTransparent);
	Vram.Upload({{64 + 7, 7, 1, 1}, {Red}});
	CHECK(TexelKindsWithin(Vram, Page, {}, {4, 4, 7, 7}, 0) == TexelKinds::Any);
	CHECK(TexelKindsWithin(Vram, Page, {}, {4, 4, 7, 7}, 0, true) == TexelKinds::Any);
}

TEST_CASE("The kinds known of a VRAM are asked again only once it changes", "[Graphics]")
{
	FIndexedPage Rain({0, Semi | 0x7FFF});
	const uint32 View = PackTextureView(Rain.Page, Rain.Clut);
	FTexelKindCache Cache;
	Cache.Follow(Rain.Vram);
	CHECK(Cache.Kinds(View, {0, 0, 31, 31}, 0, false) == TexelKinds::SemiTransparent);
	// A palette that changes to opaque colours (a fade's step) gives other kinds from then on.
	Rain.Vram.Upload({{1, 256, 1, 1}, {Red}});
	Cache.Follow(Rain.Vram);
	CHECK(Cache.Kinds(View, {0, 0, 31, 31}, 0, false) == TexelKinds::Plain);
	// The margin and the CLUT-only question are answers of their own.
	FIndexedPage Mixed({0, Red, Red | Semi});
	Mixed.SetIndex(0, 0, 2);
	Mixed.SetIndex(5, 0, 1);
	const uint32 MixedView = PackTextureView(Mixed.Page, Mixed.Clut);
	Cache.Follow(Mixed.Vram);
	CHECK(Cache.Kinds(MixedView, {0, 0, 2, 2}, 0, false) == TexelKinds::SemiTransparent);
	CHECK(Cache.Kinds(MixedView, {0, 0, 2, 2}, 0, true) == TexelKinds::Any);
	CHECK(Cache.Kinds(MixedView, {2, 2, 3, 3}, 2, false) == TexelKinds::Any);
	CHECK(Cache.Kinds(MixedView, {0, 0, 2, 2}, 0, false) == TexelKinds::SemiTransparent);
}
