#include "Graphics/Texture.h"

namespace Weltall
{
	namespace
	{
		/** U and V are bytes, so a primitive addresses at most 256 x 256 texels of its page. */
		constexpr int32 AddressableTexels = 256;
	}

	FTexturePage FTexturePage::FromAttribute(uint16 Attribute)
	{
		FTexturePage Page;
		Page.X = (Attribute & 0x0F) * 64;
		Page.Y = (Attribute >> 4 & 1) * 256;
		Page.Blend = static_cast<EBlendMode>(Attribute >> 5 & 3);
		// Depth 3 is reserved; the GPU treats it as 15-bit.
		const uint32 Depth = Attribute >> 7 & 3;
		Page.Depth = Depth == 0 ? ETextureDepth::Indexed4 : Depth == 1 ? ETextureDepth::Indexed8 : ETextureDepth::Direct15;
		return Page;
	}

	FTexturePage FTexturePage::FromPortAttribute(uint16 Attribute)
	{
		FTexturePage Page = FromAttribute(Attribute);
		Page.Y += (Attribute >> 11 & 1) * FVram::Height;
		return Page;
	}

	FClutPosition FClutPosition::FromAttribute(uint16 Attribute)
	{
		return {(Attribute & 0x3F) * 16, Attribute >> 6 & 0x1FF};
	}

	FClutPosition FClutPosition::FromPortAttribute(uint16 Attribute)
	{
		return {(Attribute & 0x3F) * 16, Attribute >> 6 & 0x3FF};
	}

	uint16 FetchTexel(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut, uint8 U, uint8 V)
	{
		const int32 Y = Page.Y + V;
		switch (Page.Depth)
		{
		case ETextureDepth::Indexed4:
		{
			const uint16 Unit = Vram.TexelAt(Page.X + U / 4, Y);
			return Vram.TexelAt(Clut.X + ((Unit >> (U % 4 * 4)) & 0x0F), Clut.Y);
		}
		case ETextureDepth::Indexed8:
		{
			const uint16 Unit = Vram.TexelAt(Page.X + U / 2, Y);
			return Vram.TexelAt(Clut.X + ((Unit >> (U % 2 * 8)) & 0xFF), Clut.Y);
		}
		case ETextureDepth::Direct15:
			return Vram.TexelAt(Page.X + U, Y);
		}
		return 0;
	}

	namespace
	{
		/** Every addressable texel of a page and CLUT as RGBA, with the alpha a texel value maps to. */
		template <typename TAlpha>
		FImageRgba8 DecodeTexels(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut, TAlpha Alpha)
		{
			FImageRgba8 Image;
			Image.Width = AddressableTexels;
			Image.Height = AddressableTexels;
			Image.Pixels.resize(static_cast<std::size_t>(AddressableTexels * AddressableTexels * 4));
			uint8* Pixel = Image.Pixels.data();
			for (int32 V = 0; V < AddressableTexels; ++V)
			{
				for (int32 U = 0; U < AddressableTexels; ++U)
				{
					const uint16 Texel = FetchTexel(Vram, Page, Clut, static_cast<uint8>(U), static_cast<uint8>(V));
					*Pixel++ = WidenChannel(Texel & 0x1F);
					*Pixel++ = WidenChannel(Texel >> 5 & 0x1F);
					*Pixel++ = WidenChannel(Texel >> 10 & 0x1F);
					*Pixel++ = Alpha(Texel);
				}
			}
			return Image;
		}
	}

	FImageRgba8 DecodeOpaqueTexture(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut)
	{
		return DecodeTexels(Vram, Page, Clut, [](uint16 Texel) { return static_cast<uint8>(Texel == 0 ? 0 : 255); });
	}

	FImageRgba8 DecodeTexture(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut)
	{
		return DecodeTexels(Vram, Page, Clut, [](uint16 Texel)
		{
			return static_cast<uint8>(Texel == 0 ? 0 : (Texel & 0x8000) != 0 ? SemiTransparentTexelAlpha : 255);
		});
	}
}
