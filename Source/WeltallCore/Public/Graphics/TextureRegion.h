#pragma once

#include "Foundation/AssetKey.h"
#include "Foundation/Sha1.h"
#include "Foundation/Types.h"
#include "Graphics/Texture.h"

#include <array>
#include <compare>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Weltall
{
	/**
	 * A texture page read through a CLUT, packed into 32 bits: the page's column (bits 0-3) and row (bit 4), its depth
	 * (bits 5-6), bit 7 for a page of the port's rows, the CLUT's x in sixteens (bits 8-13) and its y (bits 14-23). The
	 * page's blend mode is left out: it changes how the texels are drawn, not which they are.
	 */
	WELTALLCORE_API uint32 PackTextureView(const FTexturePage& Page, const FClutPosition& Clut);
	WELTALLCORE_API FTexturePage TextureViewPage(uint32 View);
	WELTALLCORE_API FClutPosition TextureViewClut(uint32 View);
	inline constexpr uint32 NoTextureView = 0xFFFFFFFF;

	/**
	 * Texels of one view that are drawn as one picture: a rectangle of its page, U and V its first texel. A region may
	 * be the part of a larger picture the view's page reaches (a TIM wider than a page, or one the view's page begins
	 * within): the picture then lies at PictureU, PictureV in the view's texels, before the page or past it, and it is
	 * the whole picture that the region's key, decoding and replacement stand for.
	 */
	struct FTextureRegion
	{
		uint32 View = NoTextureView;
		uint8 U = 0;
		uint8 V = 0;
		/** 1 to 256 texels, within the page. */
		uint16 Width = 0;
		uint16 Height = 0;
		/** For part of a larger picture, its first texel and size; all zero when the region is its whole picture. */
		int16 PictureU = 0;
		int16 PictureV = 0;
		uint16 PictureWidth = 0;
		uint16 PictureHeight = 0;

		bool operator==(const FTextureRegion&) const = default;
		auto operator<=>(const FTextureRegion&) const = default;

		bool Contains(int32 AtU, int32 AtV) const { return AtU >= U && AtU < U + Width && AtV >= V && AtV < V + Height; }
		bool IsPart() const { return PictureWidth != 0; }
		/** The picture the region shows, in the view's texels: itself, or the larger picture it is part of. */
		int32 PictureFirstU() const { return IsPart() ? PictureU : U; }
		int32 PictureFirstV() const { return IsPart() ? PictureV : V; }
		int32 PictureTexelsWide() const { return IsPart() ? PictureWidth : Width; }
		int32 PictureTexelsHigh() const { return IsPart() ? PictureHeight : Height; }
	};

	/** The VRAM units the picture a region shows lies in, its columns counted on past VRAM's right edge where its page wraps. */
	WELTALLCORE_API FVramRect TextureRegionUnits(const FTextureRegion& Region);

	/** The rectangle from the first texel to the last, both included; the last are raised to the first where they lie before them. */
	WELTALLCORE_API FTextureRegion TextureRegionBetween(uint32 View, uint8 FirstU, uint8 FirstV, uint8 LastU, uint8 LastV);

	/** Every texel a view addresses. */
	WELTALLCORE_API FTextureRegion WholeTextureView(uint32 View);

	/**
	 * Pictures from the rectangles polygons read: those of one view that overlap or share an edge merged into one,
	 * until none do. Sorted by view, then row, then column.
	 */
	WELTALLCORE_API std::vector<FTextureRegion> MergeTextureRegions(std::vector<FTextureRegion> Footprints);

	/** One picture per view: the rectangle around all of its footprints, as a sheet of cells packed apart is one picture. Sorted by view. */
	WELTALLCORE_API std::vector<FTextureRegion> BoundTextureRegions(std::span<const FTextureRegion> Footprints);

	/** What the units of a region's picture hold before its CLUT colours them: a digest of its indices (or colours) and the indices used. */
	struct FTextureRegionTexels
	{
		FSha1::FDigest Digest{};
		std::array<uint64, 4> UsedIndices{};
	};

	WELTALLCORE_API FTextureRegionTexels ReadTextureRegionTexels(const FVram& Vram, const FTextureRegion& Region);

	/**
	 * The identity of the picture a region shows, wherever it lies: its size, its texels and, for an indexed page, the
	 * colours its CLUT gives the indices it uses. A colour is compared with the two low bits of each channel set, which
	 * is how the field brightens a map's palettes in action mode, so a texture keeps its identity there. It is an asset
	 * key (Weltall::AssetKeyName names its files).
	 */
	WELTALLCORE_API uint64 TextureRegionKey(const FVram& Vram, const FTextureRegion& Region, const FTextureRegionTexels& Texels);
	WELTALLCORE_API uint64 TextureRegionKey(const FVram& Vram, const FTextureRegion& Region);
	/** The key of the texels read through Palette (none for a direct page) rather than the view's CLUT. */
	WELTALLCORE_API uint64 TextureRegionKeyWith(const FTextureRegionTexels& Texels, std::span<const uint16> Palette);

	/** The texels of the region's picture as RGBA: texel 0 transparent (alpha 0), every other opaque whatever its bit 15. */
	WELTALLCORE_API FImageRgba8 DecodeTextureRegion(const FVram& Vram, const FTextureRegion& Region);
	/** The same through Palette (none for a direct page) rather than the view's CLUT. */
	WELTALLCORE_API FImageRgba8 DecodeTextureRegionWith(const FVram& Vram, const FTextureRegion& Region, std::span<const uint16> Palette);

	/** How a picture that carries its texels' kinds marks each in its alpha, as Beetle PSX HW's texture packs do. */
	namespace TexelKindAlpha
	{
		inline constexpr uint8 Transparent = 0;
		/** Bit 15 set: blended where its polygon is semi-transparent. */
		inline constexpr uint8 SemiTransparent = 128;
		inline constexpr uint8 Plain = 255;
	}

	/** The texels of the region's picture as DecodeTextureRegion gives them, each texel's alpha its kind (TexelKindAlpha). */
	WELTALLCORE_API FImageRgba8 DecodeTextureRegionKinds(const FVram& Vram, const FTextureRegion& Region);

	/** The colours a view's CLUT holds: 16 for a 4-bit page, 256 for an 8-bit one, none for a direct one. */
	WELTALLCORE_API std::span<const uint16> ReadTextureViewClut(const FVram& Vram, uint32 View, std::array<uint16, 256>& Colors);
}
