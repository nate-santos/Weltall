#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/BattlingTrailsRendering.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattlingTrailsRenderingTest, "Weltall.Rendering.BattlingTrailsDimTheFrameBeforeAndLeaveOutItsBlack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBattlingTrailsRenderingTest::RunTest(const FString&)
{
	// The frame before: white over [16, 32), black elsewhere; drawn over a grey picture. White's 31 in five bits becomes 27
	// (70h/80h of it, rounded down), the black leaves the grey as it was.
	const FIntPoint Size(64, 64);
	const TArray<FColor> Pixels = RenderPassesOffscreen(Size, [Size](FRDGBuilder& GraphBuilder, FRDGTextureRef Output)
	{
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Size, Output->Desc.Format, FClearValueBinding::Black,
			TexCreate_RenderTargetable | TexCreate_ShaderResource);
		FRDGTextureRef Before = GraphBuilder.CreateTexture(Desc, TEXT("BattlingTrailsTestBefore"));
		AddClearRenderTargetPass(GraphBuilder, Before, FLinearColor::Black);
		AddClearRenderTargetPass(GraphBuilder, Before, FLinearColor::White, FIntRect(16, 16, 32, 32));
		AddClearRenderTargetPass(GraphBuilder, Output, FLinearColor(0.25f, 0.25f, 0.25f));
		AddBattlingTrailsPass(GraphBuilder, Before, Output, FIntRect(FIntPoint::ZeroValue, Size));
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
	Near(TEXT("the white, dimmed"), 20, 20, 27 * 255 / 31);
	Near(TEXT("the black, left out"), 40, 40, 64);
	return true;
}

#endif
