#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/ScaledFrameRendering.h"
#include "Screens/GameScreen.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	class FQuadrantFrame final : public IScreenFrame
	{
	public:
		mutable FIntPoint RequestedSize;
		virtual void AddPasses(FRDGBuilder& Graph, FRDGTextureRef Output, const FIntRect& Area) const override
		{
			RequestedSize = Area.Size();
			const FIntPoint Middle = Area.Min + Area.Size() / 2;
			AddClearRenderTargetPass(Graph, Output, FLinearColor::Red, FIntRect(Area.Min, Middle));
			AddClearRenderTargetPass(Graph, Output, FLinearColor::Green,
				FIntRect(FIntPoint(Middle.X, Area.Min.Y), FIntPoint(Area.Max.X, Middle.Y)));
			AddClearRenderTargetPass(Graph, Output, FLinearColor::Blue,
				FIntRect(FIntPoint(Area.Min.X, Middle.Y), FIntPoint(Middle.X, Area.Max.Y)));
			AddClearRenderTargetPass(Graph, Output, FLinearColor::White, FIntRect(Middle, Area.Max));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScaledFrameRenderingTest, "Weltall.Rendering.ScaledPicturePreservesPlacementAndNativePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FScaledFrameRenderingTest::RunTest(const FString&)
{
	const FIntPoint Size(240, 140);
	const FIntRect Display(FIntPoint(20, 10), FIntPoint(220, 130));
	for (float Scale : {1.0f, 0.75f, 0.5f})
	{
		FQuadrantFrame Frame;
		const TArray<FColor> Pixels = RenderPassesOffscreen(Size, [&](FRDGBuilder& Graph, FRDGTextureRef Output)
		{
			AddClearRenderTargetPass(Graph, Output, FLinearColor::Black);
			AddScaledDisplayFramePasses(Graph, Output, Display, {}, &Frame, Scale);
		});
		if (!TestEqual(TEXT("Readback size"), Pixels.Num(), Size.X * Size.Y))
		{
			return false;
		}
		TestEqual(TEXT("Frame receives the scaled picture width"), Frame.RequestedSize.X, FMath::RoundToInt(200 * Scale));
		TestEqual(TEXT("Frame receives the scaled picture height"), Frame.RequestedSize.Y, FMath::RoundToInt(120 * Scale));
		TestEqual(TEXT("Top left"), Pixels[30 * Size.X + 40], FColor::Red);
		TestEqual(TEXT("Top right"), Pixels[30 * Size.X + 180], FColor::Green);
		TestEqual(TEXT("Bottom left"), Pixels[100 * Size.X + 40], FColor::Blue);
		TestEqual(TEXT("Bottom right"), Pixels[100 * Size.X + 180], FColor::White);
		TestEqual(TEXT("Outside picture is untouched"), Pixels[5 * Size.X + 5], FColor::Black);
	}
	return true;
}
#endif
