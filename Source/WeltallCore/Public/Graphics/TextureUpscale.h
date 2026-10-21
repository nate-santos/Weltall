#pragma once

#include "Graphics/Texture.h"
#include "Graphics/Vram.h"

#include <span>
#include <vector>

namespace Weltall
{
	/**
	 * VRAM's texels read at one indexed depth, or an upscaling of them: each unit's indices side by side, the lowest
	 * bits' first, so 4096 (4-bit) or 2048 (8-bit) across and 512 down, times the upscaling. Reads wrap around the
	 * edges, as the GPU's addressing of VRAM does. Edges, where given, holds each texel's VramEdge flags: the edges
	 * of a separate texture it lies on; none given, no texel lies on one.
	 */
	struct FTextureIndices
	{
		int32 Width = 0;
		int32 Height = 0;
		std::vector<uint8> Indices;
		std::vector<uint8> Edges = {};

		/** Width and Height are powers of two. */
		std::size_t Place(int32 X, int32 Y) const
		{
			return static_cast<std::size_t>(Y & (Height - 1)) * static_cast<std::size_t>(Width) + static_cast<std::size_t>(X & (Width - 1));
		}

		uint8 At(int32 X, int32 Y) const { return Indices[Place(X, Y)]; }

		uint8 EdgesAt(int32 X, int32 Y) const { return Edges.empty() ? 0 : Edges[Place(X, Y)]; }
	};

	/**
	 * The texture upscaler: VRAM's indexed texels made Factor times finer by picking, for each new texel, one of the
	 * indices about it, never a blend. A palette swap, a palette that animates and the transparent index work on the
	 * upscaled texels as on the original ones, whatever palette is in place when the upscaling runs. The GPU runs the
	 * same rule (TextureUpscale.usf), and this is what its output is held to.
	 */
	namespace TextureUpscale
	{
		/** How many times finer Upscale makes the texels: Double twice. */
		inline constexpr int32 Factor = 4;

		/** How far beside a texel the upscaled texels within it may take their indices from: a texel for each doubling. */
		inline constexpr int32 Reach = 2;

		/** VRAM read as Depth's indices (Indexed4 or Indexed8), with their edges. */
		WELTALLCORE_API FTextureIndices ReadIndices(const FVram& Vram, ETextureDepth Depth);

		/**
		 * The texel at (X, Y) of Image doubled: a quarter of the texel at (X / 2, Y / 2), which keeps that texel's index
		 * unless it lies at a corner where the texel is one step of a diagonal staircase. That is when its two
		 * neighbours beside the corner share an index the two across from them do not, and either they are a line one
		 * texel wide passing between the texel and the one behind the corner (which has the texel's index), or the
		 * staircase goes on along the diagonal through the corner (a neighbour of the texel on that diagonal, beyond
		 * one of the two, has the texel's index) and the two neighbours' index is the higher; the quarter then takes
		 * the two neighbours' index. Where that line crosses one of the texel's own (the texel's line has the same
		 * claim from the neighbours' side), the higher index takes the corner. Of the two sides of a diagonal edge only one has its corners
		 * taken, so one-texel steps become a smooth diagonal; transparency (index 0) never takes a corner from what
		 * it surrounds. A block, a single texel and a checkerboard keep their shapes, and a line one texel wide
		 * stays joined. Nothing is read beyond the edges of a separate texture the texel lies on: the texel's own
		 * row or column stands in for what lies across.
		 */
		WELTALLCORE_API uint8 DoubledTexel(const FTextureIndices& Image, int32 X, int32 Y);

		/** Image twice as wide and tall, every texel DoubledTexel's, each quarter on the edges of its texel's on its own sides. */
		WELTALLCORE_API FTextureIndices Double(const FTextureIndices& Image);

		/** VRAM read as Depth's indices, doubled until Factor. */
		WELTALLCORE_API FTextureIndices Upscale(const FVram& Vram, ETextureDepth Depth);

		/**
		 * The rectangles of VRAM (in units) over which upscaled texels can differ once the Changed tiles have changed:
		 * the tiles and their neighbours, wrapping around VRAM's edges, as runs along each row of tiles, a run joined
		 * with the same run in the rows below it. None crosses VRAM's edge.
		 */
		WELTALLCORE_API std::vector<FVramRect> RectsToRedo(const FVramTiles& Changed);

		/** The texels of an image Scale times finer than Depth's reading of VRAM that lie over Rect of VRAM (in units). */
		struct FTexelRect
		{
			int32 X = 0;
			int32 Y = 0;
			int32 Width = 0;
			int32 Height = 0;
		};
		WELTALLCORE_API FTexelRect TexelsOver(const FVramRect& Rect, ETextureDepth Depth, int32 Scale);

		/**
		 * Brings Doubled (Double of Depth's reading of Vram) and Upscaled (Double of Doubled) up to date over Rects,
		 * from RectsToRedo: the rest is what it was.
		 */
		WELTALLCORE_API void Redo(const FVram& Vram, ETextureDepth Depth, std::span<const FVramRect> Rects, FTextureIndices& Doubled, FTextureIndices& Upscaled);
	}
}
