#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Menu/MenuPrimitive.h"

#include <span>
#include <vector>

namespace Weltall
{
	/** One rectangle of a sprite-sheet frame: its texels, its place from the frame's origin, its texture. */
	struct FMenuSpritePart
	{
		uint16 U = 0;
		uint16 V = 0;
		int16 Width = 0;
		int16 Height = 0;
		int16 X = 0;
		int16 Y = 0;
		/** The texture's depth for its page: 0 4-bit, 1 8-bit, 2 15-bit. */
		int16 Depth = 0;
		int16 ClutX = 0;
		int16 ClutY = 0;
		/** Where the image lies in VRAM, from which the texture page is taken. */
		int16 ImageX = 0;
		int16 ImageY = 0;
		bool bFlipX = false;
		bool bFlipY = false;
	};

	/** What a frame's first part tells of its texture, as the kernel's sprite sheets report it. */
	struct FMenuSpriteTexture
	{
		int32 Parts = 0;
		int32 Depth = 0;
		int32 ClutX = 0;
		int32 ClutY = 0;
		/** The first texel's place in VRAM, as the kernel works it out from the part's image and texel. */
		int32 VramX = 0;
		int32 VramY = 0;
	};

	/**
	 * The button a menu sheet's frame pictures, by its role (the display draws the player's binding of
	 * it): the game's logical button, as the kernel's logical word has it; 0 for a frame that is not a
	 * button. The Deathblows' combos (× Fierce, logical Circle; △ Weak, logical Square; □ Strong, logical
	 * Triangle), Enter Name's ○ (backspace, logical Cross), the L1 and R1 hints (the previous and next
	 * member). The one place the menus map their icons. A drawn frame gives the role to its last part,
	 * the button's picture (the hints' box; their arrow is not the button).
	 */
	WELTALLCORE_API uint16 MenuButtonRole(int32 Frame);

	/**
	 * A sprite sheet, the menus' multi-part images (the orbs, labels, icons, portraits, digits): a
	 * table of frames, each a list of textured rectangles placed around the frame's origin.
	 */
	class FMenuSpriteSheet
	{
	public:
		/** The sheet as it unpacks: a count, a second word of 0, an offset per frame; each frame a part count, two unused bytes and its parts. */
		WELTALLCORE_API static TResult<FMenuSpriteSheet> Parse(std::span<const uint8> Bytes);

		int32 FrameCount() const { return static_cast<int32>(Frames.size()); }

		/** A frame's parts; empty past the sheet. */
		WELTALLCORE_API std::span<const FMenuSpritePart> Frame(int32 Number) const;

		WELTALLCORE_API FMenuSpriteTexture Texture(int32 Number) const;

		/** Frame Number made of Parts, the sheet growing with empty frames to reach it. */
		WELTALLCORE_API void SetFrame(int32 Number, std::vector<FMenuSpritePart> Parts);

		/**
		 * The frame's parts as the kernel draws them: one raw textured quad each, at (X, Y) plus the
		 * part's offset, offsets and sizes scaled by Scale (4096 = 1.0, rounded toward zero), a flipped
		 * part mirrored with its texels from one lower (none below 0). Appended to Primitives; returns
		 * how many.
		 */
		WELTALLCORE_API int32 Draw(int32 Number, int32 X, int32 Y, int32 Scale, std::vector<FMenuPrimitive>& Primitives) const;

		/**
		 * The frame drawn mirrored about its origin across (FlipX) or down (FlipY), as the kernel's
		 * flipped drawing places it: a part mirrored on the screen takes its texels from one lower.
		 */
		WELTALLCORE_API int32 DrawFlipped(int32 Number, int32 X, int32 Y, int32 Scale, bool bFlipX, bool bFlipY, std::vector<FMenuPrimitive>& Primitives) const;

	private:
		std::vector<std::vector<FMenuSpritePart>> Frames;
	};

	struct FTextureRegion;

	/**
	 * The pictures a sheet's frames draw, for texture replacements: every part's texels in its view (page, depth and CLUT),
	 * those of one view that overlap or touch merged into one (MergeTextureRegions). Whatever else lies in the sheet's pages
	 * (text typed at run time, what an earlier program left) is outside them, so their keys stay the same from one scene
	 * to the next.
	 */
	WELTALLCORE_API std::vector<FTextureRegion> MenuSheetTextureRegions(const FMenuSpriteSheet& Sheet);
}
