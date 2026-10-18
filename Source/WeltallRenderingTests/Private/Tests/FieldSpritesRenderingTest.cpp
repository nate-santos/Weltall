#include "Field/FieldFrame.h"
#include "Field/FieldSceneFrame.h"
#include "Graphics/Sprite.h"
#include "Graphics/SpriteDrawing.h"
#include "Graphics/TextureUpscale.h"
#include "Graphics/Vram.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/VramTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 ScreenWidth = 320;
	constexpr int32 ScreenHeight = 224;
	/** Texels are 8 view units: 4 pixels of the screen at depth 1000 and H 500. */
	constexpr int32 SizeShift = 3;
	constexpr uint16 Clut = 256 << 6;
	constexpr uint32 RawCode = 0x2D000000;
	constexpr uint32 ModulatedCode = 0x2C000000;
	constexpr uint32 SemiTransparent = 0x02000000;
	constexpr uint32 Neutral = 0x808080;

	/** Eight rows of a 4-bit texture at VRAM (0, 0), its CLUT at (0, 256): transparent, red, semi-transparent green, white, twice across. */
	Weltall::FVram MakeVram()
	{
		Weltall::FVram Vram;
		Vram.Upload({{0, 0, 2, 8}, std::vector<uint16>(16, 0x3210)});
		Vram.Upload({{0, 256, 4, 1}, {0x0000, 0x001F, 0x83E0, 0x7FFF}});
		return Vram;
	}

	/** A sprite of one part, 4 texels by 8, standing at (X, Y) of the view, 1000 ahead. */
	Weltall::FSprite MakeSprite(int32 X, int32 Y, uint32 ColorCode, uint32 DrawFlags = 0)
	{
		Weltall::FSprite Sprite;
		Sprite.bHasTransform = true;
		Sprite.Scale = 0x1000;
		Sprite.Position = {X << 16, Y << 16, 0};
		Sprite.DrawFlags = DrawFlags;
		Sprite.BuildFlags = 1u << Weltall::SpriteBuildFlags::PartCountShift | static_cast<uint32>(SizeShift) << Weltall::SpriteBuildFlags::SizeShiftShift;
		Weltall::FSpritePart& Part = Sprite.Parts[0];
		Part.Width = 4;
		Part.Height = 8;
		Part.TexturePage = 0;
		Part.Clut = Clut;
		Part.ColorCode = ColorCode;
		Part.Flags = 4;
		return Sprite;
	}

	/** The characters' side of a frame: sprites drawn as the kernel draws them, in the order given. */
	class FTestFrame
	{
	public:
		FTestFrame()
		{
			View.View = Weltall::FGteMatrix::Identity();
			View.View.T = {0, 0, 1000};
			View.Scene = View.View;
			View.ScreenRotation = Weltall::FGteMatrix::Identity();
			View.Billboard = Weltall::FGteMatrix::Identity();
			View.ProjectionDistance = 500;
			Frame.bSceneDrawn = true;
			Frame.Background = {64, 64, 64};
			Frame.View = View;
		}

		/** A sprite drawn as the kernel draws it, Key pairing it with its drawing in another tick's frame. */
		void Draw(const Weltall::FSprite& Sprite, int32 OtIndex, uint32 Key = 0)
		{
			std::vector<Weltall::FSpriteQuad> Quads;
			const Weltall::FGteMatrix Matrix = Weltall::SpriteDrawMatrix(Sprite, Weltall::FGteMatrix::Identity(), View.View);
			Weltall::DrawSpriteParts(Sprite, Matrix, OtIndex, &Quads);
			Weltall::FFieldCharacterView& Character = Frame.Characters.emplace_back();
			Character.BlendKey = Key;
			Character.Origin = {static_cast<float>(Matrix.T[0]), static_cast<float>(Matrix.T[1]), static_cast<float>(Matrix.T[2])};
			for (const Weltall::FSpriteQuad& Quad : Quads)
			{
				Frame.Order.push_back({static_cast<uint32>(Frame.Quads.size()), Weltall::EFieldDrawKind::Character});
				Frame.Quads.push_back({Quad, static_cast<uint16>(Frame.Characters.size() - 1)});
			}
		}

		/** The frame drawn Alpha of the way from Previous's, when given. */
		TArray<FColor> Render(const FVramTexture& Vram, EFieldTextureFilter SpriteFilter, const FTestFrame* Previous = nullptr, float Alpha = 1.0f,
			EFieldAntiAliasing AntiAliasing = EFieldAntiAliasing::None, FIntPoint Size = FIntPoint(ScreenWidth, ScreenHeight))
		{
			const Weltall::FFieldScene Scene;
			Weltall::EvaluateFieldScene(Scene, View, Weltall::EFieldCulling::InRange, Frame.Scenery);
			const FFieldSceneMesh Mesh(Scene);
			FFieldRenderOptions Options;
			Options.AntiAliasing = AntiAliasing;
			Options.SpriteFilter = SpriteFilter;
			FFieldSceneDraw Draw;
			if (Previous != nullptr)
			{
				Frame.Tick = Previous->Frame.Tick + 1;
			}
			BuildFieldFrameDraw(Scene, Frame, Previous != nullptr ? &Previous->Frame : nullptr, Alpha, Mesh, Options, Draw);
			return RenderFieldSceneOffscreen(Mesh, Vram, Draw, Size);
		}

		Weltall::FFieldView View;
		Weltall::FFieldFrame Frame;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldSpritesRenderingTest, "Weltall.Rendering.FieldSpritesFollowTheGpusRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldSpritesRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	FTestFrame Test;
	// Each sprite's left edge lands on the screen at 160 + X / 2; its texels are 4 pixels wide.
	Test.Draw(MakeSprite(-200, -100, RawCode | Neutral), 50);
	Test.Draw(MakeSprite(-100, -100, ModulatedCode | 0x404040), 50);
	Test.Draw(MakeSprite(0, -100, RawCode | SemiTransparent | Neutral), 50);
	Test.Draw(MakeSprite(100, -100, RawCode | Neutral, Weltall::SpriteDrawFlags::Mirrored), 50);
	// Two sprites overlapping: the first in the order is drawn first, the second over it.
	Test.Draw(MakeSprite(-200, 60, RawCode | Neutral), 60);
	Test.Draw(MakeSprite(-192, 60, ModulatedCode | 0x404040), 40);
	const TArray<FColor> Pixels = Test.Render(Units, EFieldTextureFilter::Nearest);

	// A sprite 32 view units (16 pixels) further right in the latest tick, tinted differently: half
	// way between the ticks it stands 8 pixels short of its latest place, in its latest cel and tint.
	FTestFrame Before;
	Before.Draw(MakeSprite(-232, -100, RawCode | Neutral), 50, 1);
	FTestFrame After;
	After.Draw(MakeSprite(-200, -100, ModulatedCode | 0x404040), 50, 1);
	const TArray<FColor> Between = After.Render(Units, EFieldTextureFilter::Nearest, &Before, 0.5f);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Pixels.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("both"), Between.Num(), Pixels.Num()))
	{
		return false;
	}
	const auto NearIn = [this](const TArray<FColor>& Pixels, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Pixels[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	const auto Near = [&NearIn, &Pixels](const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B) { NearIn(Pixels, What, X, Y, R, G, B); };
	// Rows 62 to 93 of the screen hold the first four sprites.
	Near(TEXT("background"), 300, 200, 64, 64, 64);
	Near(TEXT("texel 0 is transparent"), 61, 70, 64, 64, 64);
	Near(TEXT("raw red"), 65, 70, 255, 0, 0);
	Near(TEXT("semi-transparent texel of an opaque sprite"), 69, 70, 0, 255, 0);
	Near(TEXT("raw white"), 73, 70, 255, 255, 255);
	Near(TEXT("tinted at half"), 115, 70, 127, 0, 0);
	Near(TEXT("plain texel of a semi-transparent sprite"), 165, 70, 255, 0, 0);
	Near(TEXT("averaged"), 169, 70, 32, 159, 32);
	// Mirrored, the sprite's first texel column is its rightmost: white on the left.
	Near(TEXT("mirrored white"), 197, 70, 255, 255, 255);
	Near(TEXT("mirrored red"), 205, 70, 255, 0, 0);
	Near(TEXT("mirrored texel 0"), 209, 70, 64, 64, 64);
	// The sprites' rows 142 on: the second, 4 pixels right, is drawn over the first's white.
	Near(TEXT("drawn over"), 73, 150, 0, 127, 0);
	Near(TEXT("the first under it"), 65, 150, 255, 0, 0);
	NearIn(Between, TEXT("between two ticks: texel 0"), 53, 70, 64, 64, 64);
	NearIn(Between, TEXT("between two ticks: its red, tinted as in the latest"), 57, 70, 127, 0, 0);
	NearIn(Between, TEXT("between two ticks: not yet where the latest put it"), 73, 70, 64, 64, 64);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldSpritePartsMeetRenderingTest, "Weltall.Rendering.FieldSpritePartsMeetUnderMultisampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldSpritePartsMeetRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// Two parts of a sprite: the first red, green, white and a transparent texel, the second drawn over it from that
	// transparent texel's left edge on, red first. At 1.25 times the screen's size the edge falls a quarter into pixel
	// 151 of the rows, past the samples on its left and short of the pixel's centre, which is over the transparent texel.
	Weltall::FSprite Sprite = MakeSprite(-102, 0, RawCode | Neutral);
	Sprite.BuildFlags = (Sprite.BuildFlags & ~Weltall::SpriteBuildFlags::PartCountMask) | 2u << Weltall::SpriteBuildFlags::PartCountShift;
	Weltall::FSpritePart& First = Sprite.Parts[0];
	First.U = 1;
	Weltall::FSpritePart& Second = Sprite.Parts[1];
	Second = First;
	Second.X = 3;
	Second.Width = 3;
	FTestFrame Test;
	Test.Draw(Sprite, 50);
	const FIntPoint Size(400, 280);
	const TArray<FColor> Pixels = Test.Render(Units, EFieldTextureFilter::Nearest, nullptr, 1.0f, EFieldAntiAliasing::Msaa, Size);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), Size.X * Size.Y))
	{
		return false;
	}
	constexpr int32 Row = 160;
	const auto At = [&Pixels, &Size](int32 X) { return Pixels[Row * Size.X + X]; };
	TestTrue(TEXT("the first part's white"), At(150) == FColor(255, 255, 255));
	TestTrue(TEXT("the second part's red"), At(153) == FColor(255, 0, 0));
	// The pixel's samples show the first part's white and the second's red, none the background between them.
	const FColor Edge = At(151);
	TestTrue(FString::Printf(TEXT("the parts meet: (%d, %d, %d) is white and red"), Edge.R, Edge.G, Edge.B), Edge.R >= 250 && Edge.G >= 32 && Edge.G == Edge.B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldCrispTexelsRenderingTest, "Weltall.Rendering.FieldCrispTexelsAreSharpButWhereTheirEdgesCrossAPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldCrispTexelsRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);
	// Texels 4 pixels wide whose edges fall between pixels (the sprite's left edge at x 60), and the same half a pixel
	// further right (one view unit), the edge between its red and green texels then crossing pixel 68 in its middle.
	FTestFrame Whole;
	Whole.Draw(MakeSprite(-200, -100, RawCode | Neutral), 50);
	FTestFrame Half;
	Half.Draw(MakeSprite(-199, -100, RawCode | Neutral), 50);
	const TArray<FColor> Sharp = Whole.Render(Units, EFieldTextureFilter::Nearest);
	const TArray<FColor> Crisp = Whole.Render(Units, EFieldTextureFilter::Crisp);
	const TArray<FColor> Moved = Half.Render(Units, EFieldTextureFilter::Crisp);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Sharp.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("all three"), Crisp.Num() + Moved.Num(), 2 * Sharp.Num()))
	{
		return false;
	}
	int32 Differing = 0;
	for (int32 Pixel = 0; Pixel < Sharp.Num(); ++Pixel)
	{
		const FColor& A = Sharp[Pixel];
		const FColor& B = Crisp[Pixel];
		Differing += FMath::Abs(A.R - B.R) > 2 || FMath::Abs(A.G - B.G) > 2 || FMath::Abs(A.B - B.B) > 2 ? 1 : 0;
	}
	TestEqual(TEXT("texels whole pixels wide: crisp as sharp"), Differing, 0);
	const auto Near = [this, &Moved](const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B, int32 Margin)
	{
		const FColor& Pixel = Moved[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= Margin && FMath::Abs(Pixel.G - G) <= Margin && FMath::Abs(Pixel.B - B) <= Margin;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(TEXT("inside the red texel"), 66, 70, 255, 0, 0, 2);
	Near(TEXT("the pixel the edge crosses: half red, half green"), 68, 70, 127, 127, 0, 16);
	Near(TEXT("inside the green texel"), 70, 70, 0, 255, 0, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldUpscaledTexelsRenderingTest, "Weltall.Rendering.FieldUpscaledTexelsSmoothAStaircase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldUpscaledTexelsRenderingTest::RunTest(const FString&)
{
	// The sprite's texels: a staircase of red (1) under white (3), whose steps' corners the higher index takes.
	Weltall::FVram Vram = MakeVram();
	Vram.Upload({{0, 0, 1, 8}, {0x3111, 0x3311, 0x3331, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333}});
	const Weltall::FTextureIndices Upscaled = Weltall::TextureUpscale::Upscale(Vram, Weltall::ETextureDepth::Indexed4);
	TestEqual(TEXT("the red texel (1, 1) keeps its first upscaled texel"), static_cast<int32>(Upscaled.At(4, 4)), 1);
	TestEqual(TEXT("and gives its last, at the step's corner, to the white"), static_cast<int32>(Upscaled.At(7, 7)), 3);
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.SetUpscaled(true);
	Units.Follow(Vram);
	// At four times the screen's size the sprite's left edge lands at x 240 and its top at y 248, each texel 16 pixels
	// and each upscaled texel 4.
	FTestFrame Test;
	Test.Draw(MakeSprite(-200, -100, RawCode | Neutral), 50);
	const FIntPoint Size(4 * ScreenWidth, 4 * ScreenHeight);
	const TArray<FColor> Sharp = Test.Render(Units, EFieldTextureFilter::Nearest, nullptr, 1.0f, EFieldAntiAliasing::None, Size);
	const TArray<FColor> Smoothed = Test.Render(Units, EFieldTextureFilter::Upscaled, nullptr, 1.0f, EFieldAntiAliasing::None, Size);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Sharp.Num(), Size.X * Size.Y) || !TestEqual(TEXT("both"), Smoothed.Num(), Sharp.Num()))
	{
		return false;
	}
	const auto At = [&Size](const TArray<FColor>& Pixels, int32 X, int32 Y) { return Pixels[Y * Size.X + X]; };
	TestTrue(TEXT("sharp: the step's corner is red"), At(Sharp, 269, 277) == FColor(255, 0, 0));
	TestTrue(TEXT("upscaled: the step's corner is white"), At(Smoothed, 269, 277) == FColor(255, 255, 255));
	TestTrue(TEXT("upscaled: the texel's first quarter stays red"), At(Smoothed, 257, 265) == FColor(255, 0, 0));
	TestTrue(TEXT("upscaled: the white below the step stays white"), At(Smoothed, 257, 297) == FColor(255, 255, 255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldPortRowsRenderingTest, "Weltall.Rendering.FieldSpritesDrawFromThePortsRowsOfVram",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldPortRowsRenderingTest::RunTest(const FString&)
{
	// The console's rows as MakeVram leaves them, and in the port's rows a texture of index 3 only at (0, 512) with its
	// CLUT at (0, 513), whose index 3 is green where the console's is white.
	Weltall::FVram Vram = MakeVram();
	Vram.UploadToPortRows({{0, Weltall::FVram::Height, 2, 8}, std::vector<uint16>(16, 0x3333)});
	Vram.UploadToPortRows({{0, Weltall::FVram::Height + 1, 4, 1}, {0x0000, 0x001F, 0x83E0, 0x03E0}});
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.SetUpscaled(true);
	Units.Follow(Vram);
	// The same sprite as the first of FieldSpritesFollowTheGpusRules (its left edge at x 60), its part's page and CLUT
	// with their high y bits, as the kernel builds a part placed in the port's rows.
	Weltall::FSprite Sprite = MakeSprite(-200, -100, RawCode | Neutral);
	Sprite.Parts[0].TexturePage = 1 << 11;
	Sprite.Parts[0].Clut = static_cast<uint16>((Weltall::FVram::Height + 1) << 6);
	FTestFrame Test;
	Test.Draw(Sprite, 50);
	const TArray<FColor> Sharp = Test.Render(Units, EFieldTextureFilter::Nearest);
	const TArray<FColor> Crisp = Test.Render(Units, EFieldTextureFilter::Crisp);
	const TArray<FColor> Upscaled = Test.Render(Units, EFieldTextureFilter::Upscaled);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Sharp.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("all three"), Crisp.Num() + Upscaled.Num(), 2 * Sharp.Num()))
	{
		return false;
	}
	const FColor Green(0, 255, 0);
	for (const auto& [Pixels, Filter] : {std::pair{&Sharp, TEXT("sharp")}, std::pair{&Crisp, TEXT("crisp")}, std::pair{&Upscaled, TEXT("upscaled")}})
	{
		// Texel 0 of the console's texture would be transparent and texel 3 white; the port's are all green.
		TestTrue(FString::Printf(TEXT("%s: the port's first texel"), Filter), (*Pixels)[70 * ScreenWidth + 61] == Green);
		TestTrue(FString::Printf(TEXT("%s: the port's last texel"), Filter), (*Pixels)[70 * ScreenWidth + 73] == Green);
	}
	return true;
}

#endif
