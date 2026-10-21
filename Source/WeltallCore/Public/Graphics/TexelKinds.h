#pragma once

#include "Graphics/Texture.h"

#include <unordered_map>

namespace Weltall
{
	/**
	 * The kinds of texel a primitive draws, a bit each: what each of a semi-transparent primitive's two passes, its plain
	 * texels and then its blended ones, has to draw.
	 */
	namespace TexelKinds
	{
		inline constexpr uint8 Plain = 1;
		inline constexpr uint8 SemiTransparent = 2;
		inline constexpr uint8 Any = Plain | SemiTransparent;
	}

	/** The kind a texel's colour draws as: none for 0 (transparent), semi-transparent with bit 15 set, plain otherwise. */
	constexpr uint8 TexelKindOf(uint16 Color)
	{
		return Color == 0 ? 0 : (Color & 0x8000) != 0 ? TexelKinds::SemiTransparent : TexelKinds::Plain;
	}

	/** Texels of a page, from the first to the last each way. */
	struct FTexelRect
	{
		uint8 FirstU = 0;
		uint8 FirstV = 0;
		uint8 LastU = 0;
		uint8 LastV = 0;
	};

	/**
	 * The kinds of texel a primitive reading Rect of a page through a CLUT can draw: of an indexed page the CLUT's colours
	 * when they are all of one kind, else the colours of the indices within Rect and Margin texels about it (what an
	 * upscaling of the texels may take into it), or Any with bClutOnly; of a 15-bit page the texels within. Any when the
	 * margin reaches past the page.
	 */
	WELTALLCORE_API uint8 TexelKindsWithin(const FVram& Vram, const FTexturePage& Page, const FClutPosition& Clut, const FTexelRect& Rect, int32 Margin,
		bool bClutOnly = false);

	/** TexelKindsWithin's answers for one VRAM as it stands, kept while it does not change. */
	class FTexelKindCache
	{
	public:
		/** Answers from now on for Vram, which must outlive the questions: what was known of another VRAM or revision is let go. */
		WELTALLCORE_API void Follow(const FVram& InVram);

		/** TexelKindsWithin of the page and CLUT a packed view (PackTextureView) names. */
		WELTALLCORE_API uint8 Kinds(uint32 View, const FTexelRect& Rect, int32 Margin, bool bClutOnly);

	private:
		const FVram* Vram = nullptr;
		uint32 Lineage = 0;
		uint32 Revision = 0;
		std::unordered_map<uint64, uint8> Known;
	};
}
