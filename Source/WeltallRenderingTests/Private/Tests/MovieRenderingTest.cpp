#include "Graphics/Mdec.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/MovieRendering.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovieRenderingTest, "Weltall.Rendering.MoviePictureConvertsItsColorsAndKeepsItsWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMovieRenderingTest::RunTest(const FString&)
{
	// A flat 32 x 32 picture, 32 levels above mid grey, with red chroma of 20 levels (in the MDEC's
	// sixteenths); its top 24 lines are shown 4 lines down a 32 x 32 screen.
	Weltall::FMdecPicture Picture;
	Picture.Width = 32;
	Picture.Height = 32;
	Picture.Luma.assign(32 * 32, 32 * 16);
	Picture.ChromaBlue.assign(16 * 16, 0);
	Picture.ChromaRed.assign(16 * 16, 20 * 16);
	FMoviePictureTexture Texture(Picture);
	BeginInitResource(&Texture);
	const FIntPoint Size(32, 32);
	const TArray<FColor> Pixels = RenderMovieOffscreen(Texture, Size, {32, 32}, {0, 4, 32, 24});
	BeginReleaseResource(&Texture);
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
	// Y 160; R = Y + 1.402 * 20, G = Y - 0.714 * 20, B = Y.
	Near(TEXT("inside the window"), 16, 16, 188, 146, 160);
	Near(TEXT("above the window"), 16, 2, 0, 0, 0);
	Near(TEXT("below the window"), 16, 29, 0, 0, 0);
	return true;
}

#endif
