#include "Graphics/Vram.h"
#include "Menu/MenuDrawing.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/DrawListTextures.h"

#include <vector>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	Weltall::FMenuPrimitive ScreenQuad(uint8 Code, int16 X, int16 Y, int16 Width, int16 Height, Weltall::FDrawColor Color)
	{
		Weltall::FMenuPrimitive Quad;
		Quad.Code = Code;
		for (std::size_t Corner = 0; Corner < 4; ++Corner)
		{
			Quad.Corners[Corner].X = static_cast<int16>(X + (Corner & 1) * Width);
			Quad.Corners[Corner].Y = static_cast<int16>(Y + (Corner >> 1) * Height);
			Quad.Corners[Corner].Color = Color;
		}
		return Quad;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuRenderingTest, "Weltall.Rendering.MenuFrameDrawsItsLayersWithTheGpusBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMenuRenderingTest::RunTest(const FString&)
{
	// A white picture, the menu's background grey subtracted over the whole of it, then a red quad on the
	// layer at rest, 40 x 20 around the screen's centre, all on a screen twice the original's size.
	Weltall::FMenuFrame Frame;
	Frame.Layer = Weltall::MenuFlatTransform;
	Frame.Primitives.push_back(ScreenQuad(Weltall::MenuGpu::FlatQuad, 0, 0, 320, 224, {255, 255, 255}));
	Frame.Primitives.push_back(Weltall::FMenuPrimitive::MakeDrawMode(Weltall::MenuGpu::TexturePage(0, 2, 0x180, 0)));
	Weltall::FMenuPrimitive Grey = ScreenQuad(Weltall::MenuGpu::FlatQuad | Weltall::MenuGpu::SemiTransparent, 0, 0, 320, 224, {0x80, 0x80, 0x80});
	Grey.bWholeScreen = true;
	Frame.Primitives.push_back(Grey);
	Weltall::FMenuPrimitive Red = ScreenQuad(Weltall::MenuGpu::FlatQuad, 0, 0, 0, 0, {255, 0, 0});
	Red.Space = Weltall::EMenuSpace::Layer;
	Red.Points = {{{-20, -10, 0}, {20, -10, 0}, {-20, 10, 0}, {20, 10, 0}}};
	Frame.Primitives.push_back(Red);

	Weltall::FMenuDrawBuilder Builder;
	Weltall::FDrawList List;
	Builder.Build(Frame, nullptr, 1.0f, List);
	// The list samples no texture; the set holds one all the same.
	const Weltall::FVram Vram;
	const Weltall::FDrawTexture Unused[] = {{0, {0, 0, Weltall::ETextureDepth::Indexed4, Weltall::EBlendMode::Average}, {0, 256}}};
	FDrawListTextures Textures(std::span<const Weltall::FVram>(&Vram, 1), Unused);
	BeginInitResource(&Textures);
	const FIntPoint Size(640, 448);
	const TArray<FColor> Pixels = RenderDrawListOffscreen(Textures, FDrawListFrame{&List}, Size, Weltall::MenuScreenSize);
	BeginReleaseResource(&Textures);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), Size.X * Size.Y))
	{
		return false;
	}
	const auto Near = [this, &Pixels, Size](const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Pixels[Y * Size.X + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(TEXT("the picture under the grey"), 20, 20, 127, 127, 127);
	Near(TEXT("the layer's quad at the centre"), 320, 224, 255, 0, 0);
	Near(TEXT("inside the quad's corner"), 320 - 38, 224 - 18, 255, 0, 0);
	Near(TEXT("outside the quad's corner"), 320 - 42, 224 - 22, 127, 127, 127);
	return true;
}

#endif
