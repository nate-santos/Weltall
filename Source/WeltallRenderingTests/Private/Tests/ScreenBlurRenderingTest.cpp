#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/ScreenBlurRendering.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScreenBlurRenderingTest, "Weltall.Rendering.ScreenBlurTakesTheMeanOfTwoByTwoOriginalPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FScreenBlurRenderingTest::RunTest(const FString&)
{
	// A white square over [16, 32) on black, blurred once with the original's pixels four of the target's wide: each pixel
	// the mean of itself and the pixels four to its right, below it, and both.
	const FIntPoint Size(64, 64);
	const TArray<FColor> Pixels = RenderPassesOffscreen(Size, [Size](FRDGBuilder& GraphBuilder, FRDGTextureRef Output)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, Output->Desc.Format, FClearValueBinding::Black,
			TexCreate_RenderTargetable | TexCreate_ShaderResource);
		FRDGTextureRef Picture = GraphBuilder.CreateTexture(Desc, TEXT("ScreenBlurTestPicture"));
		AddClearRenderTargetPass(GraphBuilder, Picture, FLinearColor::Black);
		AddClearRenderTargetPass(GraphBuilder, Picture, FLinearColor::White, FIntRect(16, 16, 32, 32));
		AddScreenBlurPass(GraphBuilder, Picture, Output, FIntRect(FIntPoint::ZeroValue, Size), 4.0f);
	});
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), Size.X * Size.Y))
	{
		return false;
	}
	const auto Near = [this, &Pixels, Size](const TCHAR* What, int32 X, int32 Y, int32 Level)
	{
		const FColor& Pixel = Pixels[Y * Size.X + X];
		TestTrue(FString::Printf(TEXT("%s: %d near %d"), What, Pixel.G, Level), FMath::Abs(Pixel.G - Level) <= 2);
	};
	Near(TEXT("inside, all four taps white"), 20, 20, 255);
	Near(TEXT("left of the right edge, two of four"), 30, 20, 128);
	Near(TEXT("up and left of the corner, one of four"), 13, 13, 64);
	Near(TEXT("away from it, none"), 40, 40, 0);
	return true;
}

#endif
