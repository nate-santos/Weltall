#include "Graphics/TextureRegion.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr int32 PageTexels = 256;
		/** The two low bits of each 5-bit channel: the field's action-mode palettes set them in every colour but 0. */
		constexpr uint16 BrightenedBits = 0x0C63;

		int32 TexelsInUnit(ETextureDepth Depth)
		{
			return Depth == ETextureDepth::Indexed4 ? 4 : Depth == ETextureDepth::Indexed8 ? 2 : 1;
		}

		/** A texel's index into its CLUT, or its colour on a direct page; U may lie before the page or past it, within VRAM. */
		uint16 ReadTexel(const FVram& Vram, const FTexturePage& Page, int32 U, int32 V)
		{
			const int32 Y = Page.Y + V;
			const int32 X = Page.X * TexelsInUnit(Page.Depth) + U;
			switch (Page.Depth)
			{
			case ETextureDepth::Indexed4:
				return static_cast<uint16>(Vram.TexelAt(X >> 2, Y) >> ((X & 3) * 4) & 0x0F);
			case ETextureDepth::Indexed8:
				return static_cast<uint16>(Vram.TexelAt(X >> 1, Y) >> ((X & 1) * 8) & 0xFF);
			case ETextureDepth::Direct15:
				return Vram.TexelAt(X, Y);
			}
			return 0;
		}

		/** The region's picture through Palette, each texel's alpha its kind (TexelKindAlpha) with bKinds, else only whether it is transparent. */
		FImageRgba8 DecodeTexels(const FVram& Vram, const FTextureRegion& Region, std::span<const uint16> Palette, bool bKinds)
		{
			const FTexturePage Page = TextureViewPage(Region.View);
			const bool bDirect = Page.Depth == ETextureDepth::Direct15;
			const int32 FirstU = Region.PictureFirstU();
			const int32 FirstV = Region.PictureFirstV();
			FImageRgba8 Image;
			Image.Width = Region.PictureTexelsWide();
			Image.Height = Region.PictureTexelsHigh();
			Image.Pixels.resize(static_cast<std::size_t>(Image.Width) * Image.Height * 4);
			uint8* Pixel = Image.Pixels.data();
			for (int32 V = FirstV; V < FirstV + Image.Height; ++V)
			{
				for (int32 U = FirstU; U < FirstU + Image.Width; ++U)
				{
					const uint16 Read = ReadTexel(Vram, Page, U, V);
					const uint16 Texel = bDirect ? Read : Read < Palette.size() ? Palette[Read] : 0;
					*Pixel++ = WidenChannel(Texel & 0x1F);
					*Pixel++ = WidenChannel(Texel >> 5 & 0x1F);
					*Pixel++ = WidenChannel(Texel >> 10 & 0x1F);
					*Pixel++ = Texel == 0 ? TexelKindAlpha::Transparent : bKinds && (Texel & 0x8000) != 0 ? TexelKindAlpha::SemiTransparent : TexelKindAlpha::Plain;
				}
			}
			return Image;
		}

		/** Inclusive bounds, so that a merged rectangle can grow past one byte while its pieces are joined. */
		struct FBounds
		{
			int32 FirstU = 0;
			int32 FirstV = 0;
			int32 LastU = 0;
			int32 LastV = 0;

			bool Meets(const FBounds& Other) const
			{
				return FirstU <= Other.LastU + 1 && Other.FirstU <= LastU + 1 && FirstV <= Other.LastV + 1 && Other.FirstV <= LastV + 1;
			}

			void Take(const FBounds& Other)
			{
				FirstU = std::min(FirstU, Other.FirstU);
				FirstV = std::min(FirstV, Other.FirstV);
				LastU = std::max(LastU, Other.LastU);
				LastV = std::max(LastV, Other.LastV);
			}
		};

		void MergeView(uint32 View, std::vector<FBounds>& Pieces, std::vector<FTextureRegion>& Into)
		{
			std::vector<FBounds> Merged;
			for (bool bChanged = true; bChanged;)
			{
				bChanged = false;
				Merged.clear();
				for (const FBounds& Piece : Pieces)
				{
					const auto Joined = std::find_if(Merged.begin(), Merged.end(), [&Piece](const FBounds& Candidate) { return Candidate.Meets(Piece); });
					if (Joined == Merged.end())
					{
						Merged.push_back(Piece);
						continue;
					}
					Joined->Take(Piece);
					bChanged = true;
				}
				Pieces.swap(Merged);
			}
			for (const FBounds& Piece : Pieces)
			{
				Into.push_back({View, static_cast<uint8>(Piece.FirstU), static_cast<uint8>(Piece.FirstV), static_cast<uint16>(Piece.LastU - Piece.FirstU + 1),
					static_cast<uint16>(Piece.LastV - Piece.FirstV + 1)});
			}
		}
	}

	uint32 PackTextureView(const FTexturePage& Page, const FClutPosition& Clut)
	{
		const bool bPortRows = Page.Y >= FVram::Height;
		return static_cast<uint32>(Page.X / 64 & 0xF) | static_cast<uint32>(Page.Y / 256 & 1) << 4 | static_cast<uint32>(Page.Depth) << 5
			| static_cast<uint32>(bPortRows) << 7 | static_cast<uint32>(Clut.X / 16 & 0x3F) << 8 | static_cast<uint32>(Clut.Y & 0x3FF) << 14;
	}

	FTexturePage TextureViewPage(uint32 View)
	{
		FTexturePage Page;
		Page.X = static_cast<int32>(View & 0xF) * 64;
		Page.Y = static_cast<int32>(View >> 4 & 1) * 256 + static_cast<int32>(View >> 7 & 1) * FVram::Height;
		const uint32 Depth = View >> 5 & 3;
		Page.Depth = Depth == 0 ? ETextureDepth::Indexed4 : Depth == 1 ? ETextureDepth::Indexed8 : ETextureDepth::Direct15;
		return Page;
	}

	FClutPosition TextureViewClut(uint32 View)
	{
		return {static_cast<int32>(View >> 8 & 0x3F) * 16, static_cast<int32>(View >> 14 & 0x3FF)};
	}

	FTextureRegion TextureRegionBetween(uint32 View, uint8 FirstU, uint8 FirstV, uint8 LastU, uint8 LastV)
	{
		return {View, FirstU, FirstV, static_cast<uint16>(std::max(LastU, FirstU) - FirstU + 1), static_cast<uint16>(std::max(LastV, FirstV) - FirstV + 1)};
	}

	FTextureRegion WholeTextureView(uint32 View)
	{
		return {View, 0, 0, PageTexels, PageTexels};
	}

	std::vector<FTextureRegion> MergeTextureRegions(std::vector<FTextureRegion> Footprints)
	{
		std::sort(Footprints.begin(), Footprints.end());
		std::vector<FTextureRegion> Regions;
		std::vector<FBounds> Pieces;
		for (std::size_t First = 0; First < Footprints.size();)
		{
			const uint32 View = Footprints[First].View;
			std::size_t End = First;
			Pieces.clear();
			for (; End < Footprints.size() && Footprints[End].View == View; ++End)
			{
				const FTextureRegion& Footprint = Footprints[End];
				Pieces.push_back({Footprint.U, Footprint.V, Footprint.U + Footprint.Width - 1, Footprint.V + Footprint.Height - 1});
			}
			MergeView(View, Pieces, Regions);
			First = End;
		}
		std::sort(Regions.begin(), Regions.end(), [](const FTextureRegion& A, const FTextureRegion& B)
		{
			return A.View != B.View ? A.View < B.View : A.V != B.V ? A.V < B.V : A.U < B.U;
		});
		return Regions;
	}

	std::vector<FTextureRegion> BoundTextureRegions(std::span<const FTextureRegion> Footprints)
	{
		std::vector<FTextureRegion> Sorted(Footprints.begin(), Footprints.end());
		std::sort(Sorted.begin(), Sorted.end());
		std::vector<FTextureRegion> Regions;
		for (const FTextureRegion& Footprint : Sorted)
		{
			const FBounds Piece{Footprint.U, Footprint.V, Footprint.U + Footprint.Width - 1, Footprint.V + Footprint.Height - 1};
			if (Regions.empty() || Regions.back().View != Footprint.View)
			{
				Regions.push_back(Footprint);
				continue;
			}
			FTextureRegion& Region = Regions.back();
			FBounds Bounds{Region.U, Region.V, Region.U + Region.Width - 1, Region.V + Region.Height - 1};
			Bounds.Take(Piece);
			Region = {Region.View, static_cast<uint8>(Bounds.FirstU), static_cast<uint8>(Bounds.FirstV), static_cast<uint16>(Bounds.LastU - Bounds.FirstU + 1),
				static_cast<uint16>(Bounds.LastV - Bounds.FirstV + 1)};
		}
		return Regions;
	}

	FVramRect TextureRegionUnits(const FTextureRegion& Region)
	{
		const FTexturePage Page = TextureViewPage(Region.View);
		const int32 Per = TexelsInUnit(Page.Depth);
		const int32 First = Page.X * Per + Region.PictureFirstU();
		const int32 Last = First + Region.PictureTexelsWide() - 1;
		return {static_cast<int16>(First / Per), static_cast<int16>(Page.Y + Region.PictureFirstV()), static_cast<int16>(Last / Per - First / Per + 1),
			static_cast<int16>(Region.PictureTexelsHigh())};
	}

	FTextureRegionTexels ReadTextureRegionTexels(const FVram& Vram, const FTextureRegion& Region)
	{
		const FTexturePage Page = TextureViewPage(Region.View);
		const bool bDirect = Page.Depth == ETextureDepth::Direct15;
		const int32 FirstU = Region.PictureFirstU();
		const int32 FirstV = Region.PictureFirstV();
		const int32 Width = Region.PictureTexelsWide();
		const int32 Height = Region.PictureTexelsHigh();
		FTextureRegionTexels Texels;
		FSha1 Hash;
		const uint8 Header[5] = {static_cast<uint8>(Page.Depth), static_cast<uint8>(Width), static_cast<uint8>(Width >> 8), static_cast<uint8>(Height),
			static_cast<uint8>(Height >> 8)};
		Hash.Update(Header);
		std::vector<uint8> Row(static_cast<std::size_t>(Width) * 2);
		for (int32 V = FirstV; V < FirstV + Height; ++V)
		{
			std::size_t Bytes = 0;
			for (int32 U = FirstU; U < FirstU + Width; ++U)
			{
				const uint16 Texel = ReadTexel(Vram, Page, U, V);
				Row[Bytes++] = static_cast<uint8>(Texel);
				if (bDirect)
				{
					Row[Bytes++] = static_cast<uint8>(Texel >> 8);
				}
				else
				{
					Texels.UsedIndices[Texel / 64] |= uint64{1} << (Texel % 64);
				}
			}
			Hash.Update(std::span<const uint8>(Row.data(), Bytes));
		}
		Texels.Digest = Hash.Finish();
		return Texels;
	}

	uint64 TextureRegionKey(const FVram& Vram, const FTextureRegion& Region, const FTextureRegionTexels& Texels)
	{
		std::array<uint16, 256> Colors{};
		return TextureRegionKeyWith(Texels, ReadTextureViewClut(Vram, Region.View, Colors));
	}

	uint64 TextureRegionKeyWith(const FTextureRegionTexels& Texels, std::span<const uint16> Palette)
	{
		if (Palette.empty())
		{
			return AssetKeyOfDigest(Texels.Digest);
		}
		FSha1 Hash;
		Hash.Update(Texels.Digest);
		std::array<uint8, 256 * 3> Used{};
		std::size_t Bytes = 0;
		for (std::size_t Index = 0; Index < 256; ++Index)
		{
			if ((Texels.UsedIndices[Index / 64] >> (Index % 64) & 1) == 0)
			{
				continue;
			}
			const uint16 Original = Index < Palette.size() ? Palette[Index] : 0;
			const uint16 Color = Original != 0 ? static_cast<uint16>(Original | BrightenedBits) : 0;
			Used[Bytes++] = static_cast<uint8>(Index);
			Used[Bytes++] = static_cast<uint8>(Color);
			Used[Bytes++] = static_cast<uint8>(Color >> 8);
		}
		Hash.Update(std::span<const uint8>(Used.data(), Bytes));
		return AssetKeyOfDigest(Hash.Finish());
	}

	uint64 TextureRegionKey(const FVram& Vram, const FTextureRegion& Region)
	{
		return TextureRegionKey(Vram, Region, ReadTextureRegionTexels(Vram, Region));
	}

	FImageRgba8 DecodeTextureRegion(const FVram& Vram, const FTextureRegion& Region)
	{
		std::array<uint16, 256> Colors{};
		return DecodeTextureRegionWith(Vram, Region, ReadTextureViewClut(Vram, Region.View, Colors));
	}

	FImageRgba8 DecodeTextureRegionKinds(const FVram& Vram, const FTextureRegion& Region)
	{
		std::array<uint16, 256> Colors{};
		return DecodeTexels(Vram, Region, ReadTextureViewClut(Vram, Region.View, Colors), true);
	}

	FImageRgba8 DecodeTextureRegionWith(const FVram& Vram, const FTextureRegion& Region, std::span<const uint16> Palette)
	{
		return DecodeTexels(Vram, Region, Palette, false);
	}

	std::span<const uint16> ReadTextureViewClut(const FVram& Vram, uint32 View, std::array<uint16, 256>& Colors)
	{
		const FTexturePage Page = TextureViewPage(View);
		const FClutPosition Clut = TextureViewClut(View);
		const std::size_t Count = Page.Depth == ETextureDepth::Indexed4 ? 16 : Page.Depth == ETextureDepth::Indexed8 ? 256 : 0;
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Colors[Index] = Vram.TexelAt(Clut.X + static_cast<int32>(Index), Clut.Y);
		}
		return std::span<const uint16>(Colors.data(), Count);
	}
}
