#pragma once

#include "CoreMinimal.h"
#include "Rendering/FieldSceneRendering.h"
#include "WorldMap/WorldMapFrame.h"
#include "WorldMap/WorldMapFrameBlend.h"

/**
 * The ways the world map can be presented, each the player's choice: the field's sorting (its depth
 * rule sorting the sprites by depth, or the painter's order alone), filtering and anti-aliasing, and
 * the world map's own culling. The defaults are the field's: the depth rule, sharp
 * texels, smooth polygon edges; and everything a wider view shows within the original's reach.
 */
struct FWorldMapRenderOptions
{
	EFieldSorting Sorting = EFieldSorting::DepthBufferWithScenery;
	EFieldTextureFilter Filter = EFieldTextureFilter::Nearest;
	/** The sprites' and the trees' pixel art. */
	EFieldTextureFilter SpriteFilter = EFieldTextureFilter::Nearest;
	EFieldAntiAliasing AntiAliasing = EFieldAntiAliasing::Msaa;
	Weltall::EWorldMapCulling Culling = Weltall::EWorldMapCulling::InRange;
};

/** Where a built drawing holds what moves between its frame's ticks with the horizon's scroll: the band's pieces. */
struct FWorldMapDrawLayout
{
	struct FBand
	{
		/** The band's polygon in the frame, its first corner and polygon state in the drawing, and how many pieces it was split into. */
		uint32 Polygon = 0;
		int32 FirstCorner = 0;
		int32 FirstState = 0;
		int32 Pieces = 0;
	};

	TArray<FBand> Bands;
};

/**
 * Makes the drawing of a world map tick's frame: its polygons, already in view space, drawn through the
 * field scene's passes with the world map's projection (its 216-line screen, its centre below the
 * middle), in the original's painter's order or under the field's depth rule. With a blend, each
 * anchor's polygons where the blend moves them, under its projection. Reuses Draw's memory. ScreenLines: the lines of
 * the screen the frame was projected for (the arena's frames, in the same form, have 218); ColumnScale: the width of
 * one of its columns in 320 columns' (the arena's winner screen has 640 across the same width: one half). With a layout,
 * where its horizon band lies, for MoveWorldMapFrameDraw. TexelKinds, following the VRAM the frame is drawn from, lets a
 * semi-transparent polygon's pass with no texels to draw be left out.
 */
WELTALL_API void BuildWorldMapFrameDraw(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend, const FWorldMapRenderOptions& Options,
	FFieldSceneDraw& Draw, float ScreenLines = 216.0f, float ColumnScale = 1.0f, FWorldMapDrawLayout* Layout = nullptr,
	Weltall::FTexelKindCache* TexelKinds = nullptr);

/**
 * A drawing of Frame built with its layout, made as BuildWorldMapFrameDraw would make it under another blend of the same
 * frame: the anchors' moves, the projection and the horizon's scroll, the rest left as built. False, the drawing left
 * part made, where the scroll splits the horizon's band into other pieces than built: build it again then.
 */
WELTALL_API bool MoveWorldMapFrameDraw(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend, const FWorldMapDrawLayout& Layout,
	FFieldSceneDraw& Draw, float ScreenLines = 216.0f, float ColumnScale = 1.0f);
