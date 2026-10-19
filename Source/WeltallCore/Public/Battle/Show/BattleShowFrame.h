#pragma once

#include "Battle/Show/BattleStageBackdrop.h"
#include "Field/FieldRenderOptions.h"
#include "Foundation/Types.h"
#include "Graphics/GteMatrix.h"
#include "Graphics/MdlModel.h"
#include "Graphics/SpriteDrawing.h"
#include "Menu/MenuPrimitive.h"

#include <array>
#include <vector>

namespace Weltall
{
	class FVram;
	struct FBattleObjectsFrame;

	/** What drew a quad of the frame's 3D list. */
	enum class EBattleShowDrawer : uint8
	{
		/** A sprite task: a combatant, a script's spawned sprite. */
		Sprite,
		/**
		 * A sprite's shadow (the kernel's): its quads lie flat at the floor point's depth, and once projected
		 * the GPU's first two corners of each take their average y, and the last two theirs.
		 */
		Shadow,
		/** A damage number over a combatant. */
		Number,
		/** One of the running total's bursts. */
		TotalBurst,
		/** The running total's digits. */
		Total,
		/** A target marker's pyramid. */
		Marker,
		/** A spawned sprite's dot or square (a hit's sparks). */
		Tile,
		/** A spawned sprite's line: a streak behind it, or a line to its parent. */
		Line,
		/** A sprite script's trail (a swung weapon's). */
		Trail,
		/** A model a spawned sprite draws (an effect's: a blast's shell, a beam, debris). */
		Model,
		/** An animation extension's quads over a sprite (a wave's strips, a scroll's copies), their texels as the packets hold them. */
		Extension,
	};

	/**
	 * Something the frame's 3D list draws that moves as one between steps: its quads blend toward the
	 * next step's quads of the same key.
	 */
	struct FBattleShowDrawer
	{
		/** The same for the same task from step to step, unique among the tasks alive. */
		uint32 BlendKey = 0;
		EBattleShowDrawer Kind = EBattleShowDrawer::Sprite;
		/** Where it stands in the world, whole units: one that jumps between steps is not drawn in between. */
		std::array<int16, 3> World{};
	};

	/** A quad the GTE projects: a sprite's part or a number's glyph, with the matrix its corners go through. */
	struct FBattleShowQuad
	{
		FSpriteQuad Quad;
		/** Its drawer, in FBattleShowFrame::Drawers. */
		uint16 Drawer = 0;
	};

	/** A triangle the GTE projects (a target marker's): its corners through the matrix, each with its colour. */
	struct FBattleShowTriangle
	{
		FGteMatrix Matrix;
		std::array<FGteVector, 3> Corners{};
		/** Each corner's colour, the GPU code in the first one's top byte. */
		std::array<uint32, 3> ColorCodes{};
		/** Its drawer, in FBattleShowFrame::Drawers. */
		uint16 Drawer = 0;
	};

	/**
	 * A dot or a square a spawned sprite draws at its place (the kernel's point and tile sprites): flat, facing the
	 * screen, after a drawing mode of its own that sets how it blends.
	 */
	struct FBattleShowTile
	{
		/** Its place in the world, whole units, which the frame's view projects. */
		FGteVector Place{};
		/**
		 * A square's side in the world's units: its side on the screen is how far a point that far along the world's x
		 * from its place lies from it there, a pixel at least; it stands about its place, half its side up and left.
		 */
		int16 Size = 0;
		/** A dot: one pixel of the original's screen at its place. */
		bool bDot = false;
		/** Its colour, the GPU code in the top byte (bit 1 semi-transparent). */
		uint32 ColorCode = 0;
		/** The blend mode its drawing mode sets: 0 half, 1 add, 2 subtract, 3 add a quarter. */
		uint8 Blend = 0;
		/** Its drawer, in FBattleShowFrame::Drawers. */
		uint16 Drawer = 0;
	};

	/**
	 * A line a spawned sprite draws from its place: a streak back along its motion, or a line to its parent's place.
	 * A pixel wide on the original's screen, in one colour, after a drawing mode of its own when its sprite blends.
	 */
	struct FBattleShowLine
	{
		/** The matrix its ends go through: the frame's view; the identity for a streak in screen space, whose place is the view's. */
		FGteMatrix Matrix;
		/** Its ends in the world, whole units: the sprite's place first. */
		std::array<FGteVector, 2> Ends{};
		/** Its colour, the GPU code in the top byte (bit 1 semi-transparent). */
		uint32 ColorCode = 0;
		/** The blend mode its drawing mode sets (0 half, 1 add, 2 subtract, 3 add a quarter); none: it blends as what was drawn before it left the GPU. */
		int8 Blend = -1;
		/** Its drawer, in FBattleShowFrame::Drawers. */
		uint16 Drawer = 0;
	};

	/**
	 * A quad drawn where its corners were worked out on the screen (a span of a script's trail): the corners there, the
	 * first two at the span's start and the last two at its end, how far ahead of the eye each end lies, and its
	 * texels, one column of a page across it and the rows V0 to V1 along it.
	 */
	struct FBattleShowScreenQuad
	{
		std::array<std::array<int16, 2>, 4> Corners{};
		/** The depth of the span's start and of its end in the view, for a drawing that sorts by depth. */
		std::array<int32, 2> Depths{};
		uint8 U0 = 0;
		uint8 U1 = 0;
		uint8 V0 = 0;
		uint8 V1 = 0;
		uint16 TexturePage = 0;
		uint16 Clut = 0;
		/** Its colour, the GPU code in the top byte. */
		uint32 ColorCode = 0;
		/** Its drawer, in FBattleShowFrame::Drawers. */
		uint16 Drawer = 0;
	};

	/** A model a sprite draws in the frame: the matrix its polygons' corners go through and the projection's centre for it. */
	struct FBattleShowModel
	{
		FGteMatrix Matrix;
		/** The centre its polygons are projected about: the frame's, or the screen's middle for a sprite that asks for it. */
		int32 CentreX = 0;
		int32 CentreY = 0;
		/** Its drawer, in FBattleShowFrame::Drawers. */
		uint16 Drawer = 0;
	};

	/**
	 * A polygon of a sprite's model: a triangle or a quad (its corners in the GPU's order: the last two across from the
	 * first two) through its model's matrix, flat or with a colour a corner, textured or not.
	 */
	struct FBattleShowModelPolygon
	{
		/** Its model, in FBattleShowFrame::Models, and its number among the model's primitives: the same polygon from step to step. */
		uint16 Model = 0;
		uint16 Primitive = 0;
		uint8 CornerCount = 3;
		bool bTextured = false;
		bool bGouraud = false;
		std::array<FGteVector, 4> Corners{};
		/** Each corner's colour (a flat one's first only), the GPU code in the first one's top byte (bit 0 raw texels, bit 1 semi-transparent). */
		std::array<uint32, 4> ColorCodes{};
		std::array<FMdlUv, 4> Uvs{};
		uint16 Clut = 0;
		/** The page, whose bits 5 and 6 are the blend mode of a textured polygon. */
		uint16 TexturePage = 0;
		/**
		 * The blend mode the drawing mode before an untextured polygon sets (0 half, 1 add, 2 subtract, 3 add a quarter); none:
		 * a semi-transparent one blends as what was drawn before it left the GPU.
		 */
		int8 Blend = -1;
	};

	/** What an entry of the frame's drawing order is. */
	enum class EBattleShowLink : uint8
	{
		/** A projected quad (FBattleShowFrame::Quads). */
		Quad,
		/** A projected triangle (FBattleShowFrame::Triangles). */
		Triangle,
		/** The fade layer (FBattleShowFrame::Fade). */
		Fade,
		/** The HUD's primitives (FBattleShowFrame::Hud), all of them. */
		Hud,
		/** What the stage or a 3D object linked (FBattleShowFrame::Objects' link of that number). */
		Object,
		/** The stage's backdrop (FBattleShowFrame::Backdrop), all of it: the dome's packets, then the panoramas, the second first. */
		Backdrop,
		/** A dot or a square (FBattleShowFrame::Tiles): its drawing mode, then the tile. */
		Tile,
		/** A line (FBattleShowFrame::Lines): its drawing mode when it has one, then the line. */
		Line,
		/** A quad placed on the screen (FBattleShowFrame::ScreenQuads). */
		ScreenQuad,
		/** A polygon of a sprite's model (FBattleShowFrame::ModelPolygons): its drawing mode when it has one, then the polygon. */
		ModelPolygon,
	};

	/** What one frame of the HUD draws: its primitives in the GPU's order (the ordering table's entry, last linked first). */
	struct FBattleHudFrame
	{
		std::vector<FMenuPrimitive> Primitives;
		/** The buttons its lines of text show or name (a battle event's message), as cells of its primitives. */
		std::vector<FTextButtonCell> TextButtons;
	};

	/** One entry of the drawing order: what, which, and the ordering table's entry it was linked into. */
	struct FBattleShowLink
	{
		EBattleShowLink Kind = EBattleShowLink::Quad;
		uint32 Index = 0;
		int32 Entry = 0;
	};

	/**
	 * The screen's fade layer as its task draws it: a tile over the whole screen from (-32, -32) to (320,
	 * 240), semi-transparent in one of the GPU's four blend modes, in the ordering table's entry 2 (over
	 * everything but the HUD and the numbers).
	 */
	struct FBattleShowFade
	{
		/** 0 half, 1 add, 2 subtract, 3 add a quarter. */
		uint8 Mode = 0;
		std::array<uint8, 3> Color{};
		/** An effect's script started it, not the battle's own start, escape, result or events. */
		bool bFromEffect = false;
	};

	/**
	 * The share of the fade's colour drawn: of an effect's adding fade (its flashes, added whole or a quarter) all of it,
	 * a quarter or none, as the player chose; of every other fade all of it.
	 */
	constexpr float DrawnFadeShare(const FBattleShowFade& Fade, EBattleFlashes Flashes)
	{
		if (!Fade.bFromEffect || (Fade.Mode != 1 && Fade.Mode != 3))
		{
			return 1.0f;
		}
		return Flashes == EBattleFlashes::Off ? 0.0f : Flashes == EBattleFlashes::Softer ? 0.25f : 1.0f;
	}

	/**
	 * What the renderer reads of one step of the battle's show, as
	 * the step drew it: the view the GTE projected with, the clear colour, the stage's tint layers, the
	 * sprites and the numbers as projected quads, the fade layer, the HUD's 2D list, the 3D objects' part,
	 * and VRAM as the step left it.
	 *
	 * Order holds what the show linked in the order the GPU draws it: the ordering table far to near
	 * (entry FFFh first), within an entry the last linked first. The stage and the 3D objects link theirs
	 * before the show's in every frame, so within an entry theirs are drawn after: at each entry, the
	 * show's links, then the objects', then the stage's backdrop (in the last entry), then its model's.
	 *
	 * A frame is whole from its display to the next one's: the show builds the next apart and exchanges
	 * the two, so both keep their memory and allocate only while they grow.
	 */
	struct FBattleShowFrame
	{
		/** Counts the show's frames since Begin: frames of consecutive steps blend. */
		uint32 Step = 0;
		/** The view the frame drew with (the camera's, the shake in its translation). */
		FGteMatrix View;
		/** The camera's eye and look-at, whole units, which tell a cut from a move. */
		std::array<int16, 3> Eye{};
		std::array<int16, 3> Target{};
		/** The projection's centre on the 320 x 224 screen and its distance, as the GTE held them for the tasks' drawing. */
		int32 CentreX = 0;
		int32 CentreY = 0;
		int32 ProjectionDistance = 0;
		/** The stage is drawn (a script hides it and clears the screen to black). */
		bool bStageDrawn = true;
		/** The draw buffer is cleared to ClearColor (else the stage's own background covers it), as the tint layers left it. */
		bool bCleared = false;
		std::array<uint8, 3> ClearColor{};
		/**
		 * The stage's backdrop, in the ordering table's last entry: drawn before the stage's own polygons there and after
		 * what the objects and the show link into that entry, the dome first, then the panoramas. Empty while the stage is
		 * hidden.
		 */
		FBattleBackdropFrame Backdrop;
		std::vector<FBattleShowDrawer> Drawers;
		std::vector<FBattleShowQuad> Quads;
		std::vector<FBattleShowTriangle> Triangles;
		std::vector<FBattleShowTile> Tiles;
		std::vector<FBattleShowLine> Lines;
		std::vector<FBattleShowScreenQuad> ScreenQuads;
		std::vector<FBattleShowModel> Models;
		std::vector<FBattleShowModelPolygon> ModelPolygons;
		FBattleShowFade Fade;
		/** The HUD's primitives, all of them in the ordering table's entry 1. */
		FBattleHudFrame Hud;
		/** The show's links in the GPU's drawing order. */
		std::vector<FBattleShowLink> Order;
		/** The stage's and the 3D objects' part (their parts' polygons, shadows, cloths and trails), filled by the objects; none without them. */
		const FBattleObjectsFrame* Objects = nullptr;
		/** VRAM as the frame left it, and its revision. */
		const FVram* Vram = nullptr;
		uint32 VramRevision = 0;
	};
}
