#pragma once

#include "CoreMinimal.h"
#include "Field/FieldGearDrawing.h"
#include "Field/FieldScreenInVram.h"
#include "Graphics/TexelKinds.h"
#include "Menu/MenuButtonIcons.h"
#include "Rendering/FieldSceneRendering.h"
#include "Screens/GameScreen.h"
#include "Screens/MoviePictureView.h"

#include <utility>
#include <vector>

class FFieldCoverPicture;
class FFieldScreenFrame;
class FFieldStillFrame;
class FMoviePictureTexture;
class FVramTexture;

namespace Weltall
{
	class FFieldProgram;
	struct FFieldScene;
	struct FFieldFrame;
}

/**
 * The Field program shown at the display's rate: its scenery, characters and 2D layer drawn between
 * its latest two frames, or the movie it plays full screen (over the field, with the message windows
 * over the picture; in the scene, through VRAM, where the map's models show it); and the picture of
 * the screen the field takes, drawn over it as the frames say.
 */
class FFieldView
{
public:
	explicit FFieldView(const FFieldRenderOptions& InOptions);
	~FFieldView();

	/**
	 * After each of the program's frames: VRAM as the frame left it, a new map's scenery, the 2D
	 * layer's pairing with the frame before, the movie's picture. bNewProgram: the program is another
	 * run of the field, whose maps and frames start afresh, after Shown, the last frame the display
	 * showed before it (none: black): what the screen still holds when its first frame takes a picture.
	 */
	void Follow(const Weltall::FFieldProgram& Program, bool bNewProgram, const TSharedPtr<const IScreenFrame>& Shown);

	/** How the field is drawn from the next frame the display shows. */
	void SetOptions(const FFieldRenderOptions& InOptions) { Options = InOptions; }

	/** How the player's buttons are shown in the messages' text from the next frame; none keeps the font's glyphs. It must outlive the view. */
	void SetButtonGlyphs(const Weltall::IButtonGlyphs* InGlyphs) { Glyphs = InGlyphs; }

	/** What the display shows Alpha of the way from the program's frame before its latest to the latest; nothing before its first. */
	TSharedPtr<const IScreenFrame> Capture(const Weltall::FFieldProgram& Program, float Alpha) const;

	/** The field's latest picture standing still, without the menu's shading: what a menu is drawn over. Nothing before its first. */
	TSharedPtr<const IScreenFrame> CaptureStill(const Weltall::FFieldProgram& Program) const;

private:
	/** What the display showed of a frame: the movie's picture over or instead of the field, if one was shown. */
	struct FShownMovie
	{
		TSharedPtr<FMoviePictureTexture> Picture;
		bool bOverField = false;
	};

	/** The picture of the screen a tick took: the frame shown before it, as the display showed it, with VRAM and the scenery as they were. */
	void TakePicture(const Weltall::FFieldProgram& Program);
	/**
	 * Fills a displayed frame of Now, its textures from SceneVram, drawn Alpha of the way from Before (none: as it is); whether the
	 * scene was drawn between the two.
	 */
	bool Fill(FFieldScreenFrame& Display, const Weltall::FFieldScene& Scene, const Weltall::FVram& SceneVram, const Weltall::FFieldFrame& Now,
		const Weltall::FFieldFrame* Before, float Alpha, const std::vector<int32>* LayerMatches, const FShownMovie& Shown) const;
	/** What the drawing of Now may know of its texels from SceneVram: nothing where the GPU's textures are not the core's VRAM. */
	Weltall::FTexelKindCache* TexelKindsOf(const Weltall::FVram& SceneVram, const Weltall::FFieldFrame& Now) const;

	/** The player's buttons in place of the text's button glyphs. */
	void ShowTextButtons(FFieldScreenFrame& Display, const Weltall::FFieldFrame& Now) const;

	FFieldRenderOptions Options;
	TSharedRef<FVramTexture> Vram;
	const Weltall::IButtonGlyphs* Glyphs = nullptr;
	mutable Weltall::FMenuButtonIcons Icons;
	TSharedRef<FVramTexture> IconVram;
	TSharedPtr<const FFieldSceneMesh> Mesh;
	/** The field's count of map loads and the gears' layout the mesh was built for. */
	int32 MeshMapLoads = 0;
	uint32 MeshGears = 0;
	std::vector<Weltall::FFieldGearLayoutItem> GearLayout;
	/** The 2D layer's primitives paired between the two frames. */
	std::vector<int32> Matches;
	std::vector<std::pair<uint32, int32>> MatchScratch;
	FMoviePictureView Movie;
	/** The movie the display shows of the latest frame. */
	FShownMovie ShownMovie;
	/** The picture of the screen the frames draw while the field shows it, and the count of pictures taken it is. */
	TSharedPtr<FFieldCoverPicture> Picture;
	uint32 PicturesTaken = 0;
	/** What the display showed before the program's first frame, kept until it has shown one of its own. */
	TSharedPtr<const IScreenFrame> ScreenBefore;
	/** The pictures by their count, while VRAM holds pixels of them (the latest's too), and where VRAM was last given them. */
	TMap<int32, TSharedPtr<FFieldCoverPicture>> Pictures;
	std::vector<Weltall::FFieldScreenInVram> ScreenWritten;
	/** Frames handed out; one the rendering thread no longer holds is filled again. */
	mutable TArray<TSharedRef<FFieldScreenFrame>> DisplayFrames;
	/** The latest picture standing still, and what it shows: the map's load, its tick, the options and the mesh it was drawn with. */
	mutable TSharedPtr<FFieldStillFrame> Still;
	mutable int32 StillMapLoads = 0;
	mutable uint32 StillTick = 0;
	mutable FFieldRenderOptions StillOptions;
	mutable TSharedPtr<const FFieldSceneMesh> StillMesh;
	mutable Weltall::FTexelKindCache TexelKinds;
};
