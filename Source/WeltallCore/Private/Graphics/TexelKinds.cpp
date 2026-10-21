#include "Graphics/TexelKinds.h"

#include "Graphics/TextureRegion.h"

#include <bitset>

namespace Weltall
{
	namespace
	{
		constexpr int32 LastTexel = 255;

		int32 ClutColors(ETextureDepth Depth)
		{
			return Depth == ETextureDepth::Indexed4 ? 16 : 256;
		}

		/** The index of the texel at (U, V) of an indexed page, as FetchTexel reads it. */
		uint32 IndexAt(const FVram& Vram, const FTexturePage& Page, int32 U, int32 V)
		{
			if (Page.Depth == ETextureDepth::Indexed4)
			{
				return Vram.TexelAt(Page.X + U / 4, Page.Y + V) >> (U % 4 * 4) & 0x0F;
			}
			return Vram.TexelAt(Page.X + U / 2, Page.Y + V) >> (U % 2 * 8) & 0xFF;
		}
	}

	uint8 TexelKindsWithin(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut, const FTexelRect& Rect, int32 Margin, bool bClutOnly)
	{
		const int32 FirstU = Rect.FirstU - Margin;
		const int32 FirstV = Rect.FirstV - Margin;
		const int32 LastU = Rect.LastU + Margin;
		const int32 LastV = Rect.LastV + Margin;
		const bool bWithinPage = FirstU >= 0 && FirstV >= 0 && LastU <= LastTexel && LastV <= LastTexel && FirstU <= LastU && FirstV <= LastV;
		if (Page.Depth == ETextureDepth::Direct15)
		{
			if (bClutOnly || !bWithinPage)
			{
				return TexelKinds::Any;
			}
			uint8 Kinds = 0;
			for (int32 V = FirstV; V <= LastV && Kinds != TexelKinds::Any; ++V)
			{
				for (int32 U = FirstU; U <= LastU; ++U)
				{
					Kinds |= TexelKindOf(Vram.TexelAt(Page.X + U, Page.Y + V));
				}
			}
			return Kinds;
		}
		const int32 Colors = ClutColors(Page.Depth);
		uint8 ClutKinds = 0;
		for (int32 Index = 0; Index < Colors; ++Index)
		{
			ClutKinds |= TexelKindOf(Vram.TexelAt(Clut.X + Index, Clut.Y));
		}
		if (ClutKinds != TexelKinds::Any)
		{
			return ClutKinds;
		}
		if (bClutOnly || !bWithinPage)
		{
			return TexelKinds::Any;
		}
		std::bitset<256> Used;
		for (int32 V = FirstV; V <= LastV; ++V)
		{
			for (int32 U = FirstU; U <= LastU; ++U)
			{
				Used.set(IndexAt(Vram, Page, U, V));
			}
		}
		uint8 Kinds = 0;
		for (int32 Index = 0; Index < Colors && Kinds != TexelKinds::Any; ++Index)
		{
			if (Used.test(static_cast<std::size_t>(Index)))
			{
				Kinds |= TexelKindOf(Vram.TexelAt(Clut.X + Index, Clut.Y));
			}
		}
		return Kinds;
	}

	void FTexelKindCache::Follow(const FVram& InVram)
	{
		if (Vram == nullptr || InVram.Lineage() != Lineage || InVram.Revision() != Revision)
		{
			Known.clear();
			Lineage = InVram.Lineage();
			Revision = InVram.Revision();
		}
		Vram = &InVram;
	}

	uint8 FTexelKindCache::Kinds(uint32 View, const FTexelRect& Rect, int32 Margin, bool bClutOnly)
	{
		const uint64 Key = static_cast<uint64>(View) << 32 | static_cast<uint64>(Rect.FirstU) << 24 | static_cast<uint64>(Rect.FirstV) << 16
			| static_cast<uint64>(Rect.LastU) << 8 | Rect.LastV;
		// A view's packing leaves its top bits clear: the margin and the CLUT-only question take them.
		const uint64 Asked = Key | static_cast<uint64>(Margin & 0x3F) << 57 | static_cast<uint64>(bClutOnly) << 63;
		if (const auto Found = Known.find(Asked); Found != Known.end())
		{
			return Found->second;
		}
		const uint8 Kinds = TexelKindsWithin(*Vram, TextureViewPage(View), TextureViewClut(View), Rect, Margin, bClutOnly);
		Known.emplace(Asked, Kinds);
		return Kinds;
	}
}
