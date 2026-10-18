#include "Field/FieldFrame.h"
#include "Field/FieldScene.h"
#include "Field/FieldSpriteModels.h"
#include "Graphics/Vram.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/VramTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

// The models the sprites' spawned children draw, on synthetic frames: their polygons' texels as the GPU
// draws them, what the kernel left undrawn, and a model drawn between two ticks.

namespace
{
	constexpr int32 SpriteModelScreenWidth = 320;
	constexpr int32 SpriteModelScreenHeight = 224;
	constexpr uint16 SpriteModelClut = 256 << 6;
	constexpr uint8 SpriteModelRawQuad = 0x2D;

	/** A row of a 4-bit texture at VRAM (0, 0) and its CLUT at (0, 256): transparent, red, semi-transparent green, white, four times across. */
	Weltall::FVram MakeSpriteModelVram()
	{
		Weltall::FVram Vram;
		Vram.Upload({{0, 0, 4, 1}, std::vector<uint16>(4, 0x3210)});
		Vram.Upload({{0, 256, 4, 1}, {0x0000, 0x001F, 0x83E0, 0x7FFF}});
		return Vram;
	}

	/** A frame with no scenery and the children's models given: 1000 view units ahead of the eye with H = 500, two units a pixel. */
	class FSpriteModelTestFrame
	{
	public:
		FSpriteModelTestFrame()
		{
			Scene.Models.emplace_back().Parts.emplace_back();
			Frame.View.View = Weltall::FGteMatrix::Identity();
			Frame.View.ProjectionDistance = 500;
			Frame.bSceneDrawn = true;
			Frame.Background = {64, 64, 64};
		}

		/** A child's model with one quad over [X, X + 16) x [Y, Y + 4) of the screen, its picture the strip's 16 texels; drawn or not. */
		void Model(int32 X, int32 Y, uint32 Key, bool bDrawn = true)
		{
			Weltall::FFieldSpriteModelsFrame& Models = Frame.SpriteModels;
			Weltall::FFieldSpriteModelView& View = Models.Models.emplace_back();
			View.Matrix = Weltall::FGteMatrix::Identity();
			View.Matrix.T = {(X - 160) * 2, (Y - 112) * 2, 1000};
			Weltall::FFieldSpriteModelPolygon& Polygon = Models.Polygons.emplace_back();
			Polygon.Model = static_cast<uint32>(Models.Models.size() - 1);
			Polygon.BlendKey = Key;
			Polygon.Type = Weltall::EMdlPrimitiveType::StaticTexturedQuad;
			Polygon.CornerCount = 4;
			Polygon.Corners = {{{0, 0, 0}, {32, 0, 0}, {0, 8, 0}, {32, 8, 0}}};
			Polygon.Uvs = {Weltall::FMdlUv{0, 0}, Weltall::FMdlUv{16, 0}, Weltall::FMdlUv{0, 0}, Weltall::FMdlUv{16, 0}};
			Polygon.Code = SpriteModelRawQuad;
			Polygon.Clut = SpriteModelClut;
			Polygon.Colors.fill({128, 128, 128});
			Polygon.Bucket = 1000 / 16;
			Polygon.bDrawn = bDrawn;
			if (bDrawn)
			{
				Frame.Order.push_back({static_cast<uint32>(Models.Polygons.size() - 1), Weltall::EFieldDrawKind::SpriteModel});
			}
		}

		TArray<FColor> Render(const FVramTexture& Vram, const FSpriteModelTestFrame* Previous = nullptr, float Alpha = 1.0f)
		{
			Scene.Objects.emplace_back().Placement = Weltall::FGteMatrix::Identity();
			const FFieldSceneMesh Mesh(Scene);
			Weltall::EvaluateFieldScene(Scene, Frame.View, Weltall::EFieldCulling::InRange, Frame.Scenery);
			Frame.SceneryWorld.assign(Scene.Objects.size(), {});
			FFieldRenderOptions Options;
			Options.AntiAliasing = EFieldAntiAliasing::None;
			Options.Sorting = EFieldSorting::PaintersOrder;
			FFieldSceneDraw Draw;
			if (Previous != nullptr)
			{
				Frame.Tick = Previous->Frame.Tick + 1;
			}
			BuildFieldFrameDraw(Scene, Frame, Previous != nullptr ? &Previous->Frame : nullptr, Alpha, Mesh, Options, Draw);
			return RenderFieldSceneOffscreen(Mesh, Vram, Draw, FIntPoint(SpriteModelScreenWidth, SpriteModelScreenHeight));
		}

		Weltall::FFieldScene Scene;
		Weltall::FFieldFrame Frame;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldSpriteModelRenderingTest, "Weltall.Rendering.FieldSpriteModelsFollowTheGpusRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldSpriteModelRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeSpriteModelVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	FSpriteModelTestFrame Test;
	Test.Model(20, 20, 1);
	Test.Model(20, 60, 2, false);
	const TArray<FColor> Pixels = Test.Render(Units);
	// A model 16 pixels further right in the latest tick: drawn half way.
	FSpriteModelTestFrame Before;
	Before.Model(4, 100, 9);
	FSpriteModelTestFrame After;
	After.Model(20, 100, 9);
	const TArray<FColor> Between = After.Render(Units, &Before, 0.5f);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Pixels.Num(), SpriteModelScreenWidth * SpriteModelScreenHeight)
		|| !TestEqual(TEXT("both"), Between.Num(), Pixels.Num()))
	{
		return false;
	}
	const auto Near = [this](const TArray<FColor>& Frame, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Frame[Y * SpriteModelScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(Pixels, TEXT("texel 0 is transparent"), 20, 21, 64, 64, 64);
	Near(Pixels, TEXT("a texel drawn raw"), 21, 21, 255, 0, 0);
	Near(Pixels, TEXT("an opaque polygon draws a semi-transparent texel as it is"), 22, 21, 0, 255, 0);
	Near(Pixels, TEXT("white"), 23, 21, 255, 255, 255);
	Near(Pixels, TEXT("a polygon the kernel left undrawn"), 21, 61, 64, 64, 64);
	Near(Between, TEXT("between two ticks: half way"), 13, 101, 255, 0, 0);
	Near(Between, TEXT("between two ticks: not yet where the latest put it"), 33, 101, 64, 64, 64);
	return true;
}

#endif
