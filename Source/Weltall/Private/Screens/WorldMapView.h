#pragma once

#include "CoreMinimal.h"
#include "Graphics/TexelKinds.h"
#include "Rendering/WorldMapRendering.h"
#include "Screens/GameScreen.h"

class FVramTexture;
class FWorldMapScreenFrame;

/** Some of a world map frame's 2D lists drawn as one layer, with the tick before's and their pairs (when it follows). */
struct FWorldMapLayer
{
	Weltall::FDrawList Latest;
	Weltall::FDrawList Previous;
	std::vector<int32> Matches;
	bool bPrevious = false;

	/** The lists of the two frames, in order, and their pairs, sorting in Scratch. */
	void Gather(const Weltall::FWorldMapFrame& LatestFrame, const Weltall::FWorldMapFrame* PreviousFrame,
		std::initializer_list<Weltall::FDrawList Weltall::FWorldMapFrame::*> Lists, std::vector<std::pair<uint32, int32>>& Scratch);
};

namespace Weltall
{
	class FWorldMapProgram;
}

/**
 * The world map program shown at the display's rate: its latest frame's scene and its 2D layer (the
 * minimap, the windows, the fade), textured from VRAM as the frame left it, drawn in between the tick
 * before and the latest; nothing before its first frame.
 */
class FWorldMapView
{
public:
	explicit FWorldMapView(const FWorldMapRenderOptions& InOptions);
	~FWorldMapView();

	/** How the next frames are drawn (the player's settings changed). */
	void SetOptions(const FWorldMapRenderOptions& InOptions)
	{
		Options = InOptions;
		bBuiltStale = true;
	}

	/**
	 * After each of the program's frames: VRAM as the frame left it. bNewProgram: another run of the world map, whose
	 * frames start afresh, after Shown, the last frame the display showed before it (none: black).
	 */
	void Follow(const Weltall::FWorldMapProgram& Program, bool bNewProgram, const TSharedPtr<const IScreenFrame>& Shown);

	/**
	 * What the display shows Alpha of the way from the program's frame before to its latest, or in one of its own fades
	 * the picture it darkens (Shown, the frame the display shows now, where the fade has drawn nothing yet); nothing
	 * before its first.
	 */
	TSharedPtr<const IScreenFrame> Capture(const Weltall::FWorldMapProgram& Program, float Alpha, const TSharedPtr<const IScreenFrame>& Shown) const;

	/** The map's latest picture standing still, as it was before the fade its menu opens with: what the menu is drawn over. Nothing before its first. */
	TSharedPtr<const IScreenFrame> CaptureStill(const Weltall::FWorldMapProgram& Program) const { return CaptureMap(Program, 1.0f); }

private:
	TSharedPtr<const IScreenFrame> CaptureMap(const Weltall::FWorldMapProgram& Program, float Alpha) const;

	/** The screen before the world map started, which its entry fades. */
	TSharedPtr<const IScreenFrame> ScreenBefore;
	/** The minimap and the windows, and the fade, as Follow found them; the anchors' pairs. */
	FWorldMapLayer Overlays;
	FWorldMapLayer Fade;
	std::vector<int32> AnchorMatches;
	std::vector<std::pair<uint32, int32>> MatchScratch;
	bool bPreviousFollows = false;
	mutable Weltall::FWorldMapFrameBlend Blend;
	/** The latest frame's drawing, built once a tick and moved to each display frame's blend, and where its horizon lies. */
	mutable FFieldSceneDraw BuiltDraw;
	mutable FWorldMapDrawLayout BuiltLayout;
	mutable bool bBuiltStale = true;
	/** What the drawing knows of the texels VRAM holds. */
	mutable Weltall::FTexelKindCache TexelKinds;
	FWorldMapRenderOptions Options;
	TSharedRef<FVramTexture> Vram;
	/** The field scene's passes draw a scene's mesh and the frame's own polygons; the world map's are all its own. */
	TSharedRef<const FFieldSceneMesh> NoMesh;
	/** Frames handed out; one the rendering thread no longer holds is filled again. */
	mutable TArray<TSharedRef<FWorldMapScreenFrame>> DisplayFrames;
};
