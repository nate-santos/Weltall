#pragma once

#include "Foundation/Types.h"
#include "Graphics/Vram.h"

#include <vector>

namespace Weltall
{
	enum class ETextureDepth : uint8
	{
		Indexed4,
		Indexed8,
		Direct15,
	};

	/** A 5-bit colour channel widened to 8 bits so that 31 becomes 255: how the port shows the GPU's 15-bit colours, texels among them. */
	constexpr uint8 WidenChannel(uint32 Channel)
	{
		return static_cast<uint8>(Channel << 3 | Channel >> 2);
	}

	/**
	 * An 8-bit channel of a colour the GPU fills VRAM with (a draw environment's clear colour), as the port shows it: the GPU
	 * keeps its top five bits, so the fill meets a texel of the same 15-bit colour exactly, as on the console.
	 */
	constexpr uint8 FilledChannel(uint8 Channel)
	{
		return WidenChannel(Channel >> 3u);
	}

	/** How the GPU combines a semi-transparent pixel with the one behind it (B) when drawing it (F). */
	enum class EBlendMode : uint8
	{
		/** B / 2 + F / 2 */
		Average,
		/** B + F */
		Add,
		/** B - F */
		Subtract,
		/** B + F / 4 */
		AddQuarter,
	};

	/** A primitive's texture page attribute: where its texels are and how they are stored. */
	struct FTexturePage
	{
		int32 X = 0;
		int32 Y = 0;
		ETextureDepth Depth = ETextureDepth::Indexed4;
		EBlendMode Blend = EBlendMode::Average;

		/** The attribute as the console's GPU reads it: bit 11 is not looked at, so the page lies in the console's rows. */
		WELTALLCORE_API static FTexturePage FromAttribute(uint16 Attribute);
		/**
		 * The attribute as a GPU with 2 MB of VRAM reads it, bit 11 adding 512 to the page's y (where libgpu's getTPage puts
		 * y's bit 9): a page of the port's own rows (FVram::PortRows). Only the port's sprites placed there set the bit.
		 */
		WELTALLCORE_API static FTexturePage FromPortAttribute(uint16 Attribute);

		bool operator==(const FTexturePage&) const = default;
	};

	/** A primitive's CLUT attribute: where the palette of an indexed texture is. */
	struct FClutPosition
	{
		int32 X = 0;
		int32 Y = 0;

		/** The attribute as the console's GPU reads it: bits 6 to 14 its y, bit 15 not looked at. */
		WELTALLCORE_API static FClutPosition FromAttribute(uint16 Attribute);
		/** As a GPU with 2 MB of VRAM reads it, bit 15 adding 512 to its y (getClut's y << 6): a CLUT of the port's own rows. */
		WELTALLCORE_API static FClutPosition FromPortAttribute(uint16 Attribute);

		bool operator==(const FClutPosition&) const = default;
	};

	/**
	 * The texel at (U, V) of a texture page as the GPU fetches it: a 15-bit BGR color with the
	 * semi-transparency flag in bit 15. 0 is fully transparent. A page or CLUT of the port's rows is read
	 * from them (FVram::TexelAt).
	 */
	WELTALLCORE_API uint16 FetchTexel(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut, uint8 U, uint8 V);

	/** 8-bit RGBA pixels, row by row. */
	struct FImageRgba8
	{
		int32 Width = 0;
		int32 Height = 0;
		std::vector<uint8> Pixels;
	};

	/**
	 * Every texel a primitive with this page and CLUT can address (U and V are 0-255), as drawn
	 * without semi-transparency: texel 0 is transparent and every other texel opaque, whatever its
	 * bit 15 says.
	 */
	WELTALLCORE_API FImageRgba8 DecodeOpaqueTexture(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut);

	/** The alpha DecodeTexture gives a texel with bit 15 set, which blends when its primitive is semi-transparent. */
	inline constexpr uint8 SemiTransparentTexelAlpha = 128;

	/**
	 * Every texel a primitive with this page and CLUT can address, keeping what semi-transparency
	 * needs: texel 0 has alpha 0, a texel with bit 15 set SemiTransparentTexelAlpha, any other 255.
	 */
	WELTALLCORE_API FImageRgba8 DecodeTexture(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut);
}
