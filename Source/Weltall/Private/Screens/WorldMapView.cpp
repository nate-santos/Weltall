#include "WorldMapView.h"

#include "Field/FieldScene.h"
#include "Game/WorldMapProgram.h"
#include "Presentation/DrawList.h"
#include "Presentation/ScreenLayout.h"
#include "Rendering/DrawListRendering.h"
#include "Rendering/SharedRenderResource.h"
#include "Rendering/VramTexture.h"
#include "Rendering/TextureModLibrary.h"
#include "Rendering/TextureModSources.h"

namespace
{
	/** The world map's screen, fitted in the display's middle as the field's is; the scene and the fade fill the display. */
	constexpr Weltall::FScreenSize WorldMapScreen{320, 216};

	void AddLayerPasses(FRDGBuilder& GraphBuilder, const FVramTexture& Vram, const FWorldMapLayer& Layer, float Alpha, FRDGTextureRef Output, const FIntRect& Rect)
	{
		if (!Layer.Latest.Primitives.empty())
		{
			const FDrawListFrame Frame{&Layer.Latest, Layer.bPrevious ? &Layer.Previous : nullptr, Layer.bPrevious ? &Layer.Matches : nullptr, Alpha};
			AddDrawListPasses(GraphBuilder, Vram, Frame, Output, Rect, WorldMapScreen);
		}
	}
}

void FWorldMapLayer::Gather(const Weltall::FWorldMapFrame& LatestFrame, const Weltall::FWorldMapFrame* PreviousFrame,
	std::initializer_list<Weltall::FDrawList Weltall::FWorldMapFrame::*> Lists, std::vector<std::pair<uint32, int32>>& Scratch)
{
	const auto Into = [Lists](const Weltall::FWorldMapFrame& Frame, Weltall::FDrawList& Layer)
	{
		Layer.Primitives.clear();
		for (Weltall::FDrawList Weltall::FWorldMapFrame::* List : Lists)
		{
			const Weltall::FDrawList& From = Frame.*List;
			Layer.Primitives.insert(Layer.Primitives.end(), From.Primitives.begin(), From.Primitives.end());
		}
	};
	Into(LatestFrame, Latest);
	bPrevious = PreviousFrame != nullptr;
	if (bPrevious)
	{
		Into(*PreviousFrame, Previous);
		Weltall::MatchBlendKeys(Previous, Latest, Matches, Scratch);
	}
}

/** A frame of one of the world map's own fades: a picture, the GPU subtracting a level from it over the whole display. */
class FWorldMapFadeFrame final : public IScreenFrame
{
public:
	FWorldMapFadeFrame(TSharedPtr<const IScreenFrame> InPicture, TSharedRef<FVramTexture> InVram, int32 Level, int32 Blend)
		: Picture(MoveTemp(InPicture)), Vram(MoveTemp(InVram))
	{
		const uint8 Clamped = static_cast<uint8>(FMath::Clamp(Level, 0, 255));
		const Weltall::FDrawColor Color{Clamped, Clamped, Clamped};
		Weltall::FDrawPrimitive& Darkening = Layer.Primitives.emplace_back(
			Weltall::MakeGouraudRect({0, 0, static_cast<int16>(WorldMapScreen.Width), static_cast<int16>(WorldMapScreen.Height)}, {Color, Color, Color, Color}));
		Darkening.bGouraud = false;
		Darkening.Blend = Weltall::EDrawBlend::SemiTransparent;
		Darkening.UntexturedBlend = static_cast<Weltall::EBlendMode>(Blend & 3);
	}

	virtual void AddPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FIntRect& Area) const override
	{
		if (Picture.IsValid())
		{
			Picture->AddPasses(GraphBuilder, Output, Area);
		}
		const FDrawListFrame Frame{&Layer, nullptr, nullptr, 1.0f};
		AddDrawListPasses(GraphBuilder, *Vram, Frame, Output, Area, WorldMapScreen);
	}

	virtual Weltall::FScreenSize ScreenSize() const override
	{
		return Picture.IsValid() ? Picture->ScreenSize() : WorldMapScreen;
	}

private:
	TSharedPtr<const IScreenFrame> Picture;
	TSharedRef<FVramTexture> Vram;
	Weltall::FDrawList Layer;
};

/**
 * A displayed frame of the world map: its scene, then over it the minimap and the windows in the display's middle,
 * and the fade over the whole display.
 */
class FWorldMapScreenFrame final : public IScreenFrame
{
public:
	FWorldMapScreenFrame(TSharedRef<const FFieldSceneMesh> InMesh, TSharedRef<FVramTexture> InVram) : Mesh(MoveTemp(InMesh)), Vram(MoveTemp(InVram))
	{
	}

	virtual void AddPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FIntRect& Area) const override
	{
		AddFieldScenePasses(GraphBuilder, *Mesh, *Vram, Draw, Output, Area);
		const Weltall::FScreenLayout Layout = Weltall::FScreenLayout::Fit(Area.Width(), Area.Height(), WorldMapScreen);
		const Weltall::FDisplayRect Placed = Layout.ToDisplay({0, 0, static_cast<int16>(WorldMapScreen.Width), static_cast<int16>(WorldMapScreen.Height)});
		const FIntPoint Origin = Area.Min + FIntPoint(Placed.X, Placed.Y);
		AddLayerPasses(GraphBuilder, *Vram, Overlays, Alpha, Output, FIntRect(Origin, Origin + FIntPoint(Placed.Width, Placed.Height)));
		AddLayerPasses(GraphBuilder, *Vram, Fade, Alpha, Output, Area);
	}

	virtual Weltall::FScreenSize ScreenSize() const override
	{
		return WorldMapScreen;
	}

	FFieldSceneDraw Draw;
	FWorldMapLayer Overlays;
	FWorldMapLayer Fade;
	float Alpha = 1.0f;

private:
	TSharedRef<const FFieldSceneMesh> Mesh;
	TSharedRef<FVramTexture> Vram;
};

FWorldMapView::FWorldMapView(const FWorldMapRenderOptions& InOptions)
	: Options(InOptions), Vram(ShareRenderResource(new FVramTexture())), NoMesh(MakeShared<const FFieldSceneMesh>(Weltall::FFieldScene{}))
{
}

FWorldMapView::~FWorldMapView() = default;

void FWorldMapView::Follow(const Weltall::FWorldMapProgram& Program, bool bNewProgram, const TSharedPtr<const IScreenFrame>& Shown)
{
	bBuiltStale = true;
	if (bNewProgram)
	{
		DisplayFrames.Reset();
		ScreenBefore = Shown;
	}
	if (Program.ScreenFade().Picture != Weltall::FWorldMapScreenFade::EPicture::ScreenBefore)
	{
		ScreenBefore.Reset();
	}
	const Weltall::FWorldMapFrame* Latest = Program.LatestFrame();
	if (Latest == nullptr)
	{
		return;
	}
	Vram->SetUpscaled(Weltall::UpscalesTextures(Options.Filter, Options.SpriteFilter));
	Vram->Follow(Program.Drawing().Vram());
	const Weltall::FWorldMapFrame* Previous = Program.PreviousFrame();
	bPreviousFollows = Previous != nullptr;
	// The minimap and the windows lie under the 2D layer (the fade covers them).
	Overlays.Gather(*Latest, Previous, {&Weltall::FWorldMapFrame::Minimap, &Weltall::FWorldMapFrame::Windows}, MatchScratch);
	Fade.Gather(*Latest, Previous, {&Weltall::FWorldMapFrame::Screen}, MatchScratch);
	if (bPreviousFollows)
	{
		Weltall::MatchWorldMapAnchors(*Previous, *Latest, AnchorMatches, MatchScratch);
	}
}

TSharedPtr<const IScreenFrame> FWorldMapView::Capture(const Weltall::FWorldMapProgram& Program, float Alpha, const TSharedPtr<const IScreenFrame>& Shown) const
{
	using EPicture = Weltall::FWorldMapScreenFade::EPicture;
	const Weltall::FWorldMapScreenFade& Shows = Program.ScreenFade();
	if (Shows.Picture == EPicture::None)
	{
		return CaptureMap(Program, Alpha);
	}
	if (Shows.Level < 0)
	{
		return Shown;
	}
	TSharedPtr<const IScreenFrame> Picture = Shows.Picture == EPicture::ScreenBefore ? ScreenBefore : CaptureMap(Program, 1.0f);
	return MakeShared<FWorldMapFadeFrame>(MoveTemp(Picture), Vram, Shows.Level, Shows.Blend);
}

TSharedPtr<const IScreenFrame> FWorldMapView::CaptureMap(const Weltall::FWorldMapProgram& Program, float Alpha) const
{
	const Weltall::FWorldMapFrame* Latest = Program.LatestFrame();
	if (Latest == nullptr)
	{
		return nullptr;
	}
	TSharedRef<FWorldMapScreenFrame>* Free = DisplayFrames.FindByPredicate([](const TSharedRef<FWorldMapScreenFrame>& Candidate) { return Candidate.IsUnique(); });
	TSharedRef<FWorldMapScreenFrame> Next = Free != nullptr ? *Free : DisplayFrames.Add_GetRef(MakeShared<FWorldMapScreenFrame>(NoMesh, Vram));
	const Weltall::FWorldMapFrame* Previous = bPreviousFollows ? Program.PreviousFrame() : nullptr;
	Weltall::BlendWorldMapFrames(*Latest, Previous, AnchorMatches, Alpha, Blend);
	Weltall::UnroundWorldMapBlend(*Latest, Previous, Alpha, Blend);
	Weltall::MoveWorldMapBlendToView(*Latest, Previous, Alpha, Program.LiveViewOffsets(), Blend);
	// A replacement may draw where the original's texels do not.
	Weltall::FTexelKindCache* Kinds = nullptr;
	if (!FTextureModLibrary::IsReplacing())
	{
		TexelKinds.Follow(Program.Drawing().Vram());
		Kinds = &TexelKinds;
	}
	if (bBuiltStale)
	{
		BuildWorldMapFrameDraw(*Latest, &Blend, Options, BuiltDraw, static_cast<float>(WorldMapScreen.Height), 1.0f, &BuiltLayout, Kinds);
		bBuiltStale = false;
	}
	Next->Draw = BuiltDraw;
	if (!MoveWorldMapFrameDraw(*Latest, &Blend, BuiltLayout, Next->Draw))
	{
		// The horizon's band wrapped into other pieces since the build: built again at this moment.
		BuildWorldMapFrameDraw(*Latest, &Blend, Options, BuiltDraw, static_cast<float>(WorldMapScreen.Height), 1.0f, &BuiltLayout, Kinds);
		Next->Draw = BuiltDraw;
	}
	Next->Overlays = Overlays;
	Next->Fade = Fade;
	Next->Alpha = Alpha;
	FollowTextureModSources(*Vram, Program.Drawing().Vram(), nullptr, {&Next->Draw},
		{&Latest->Minimap, &Latest->Windows, &Latest->Screen}, Latest->TextureSeeds);
	return Next;
}
