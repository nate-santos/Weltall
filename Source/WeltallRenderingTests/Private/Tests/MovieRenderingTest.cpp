#include "Graphics/Mdec.h"
#include "Media/MovieSubtitles.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovieSubtitleRenderingTest, "Weltall.Rendering.MovieSubtitleIsLaidOverThePictureByItsAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMovieSubtitleRenderingTest::RunTest(const FString&)
{
	// A flat grey 32 x 32 picture (32 levels above mid grey) under a 16 x 16 line at (8, 8): its top half opaque white,
	// its bottom half black at half alpha. Drawn at its own size, each pixel samples one texel of each.
	Weltall::FMdecPicture Picture;
	Picture.Width = 32;
	Picture.Height = 32;
	Picture.Luma.assign(32 * 32, 32 * 16);
	Picture.ChromaBlue.assign(16 * 16, 0);
	Picture.ChromaRed.assign(16 * 16, 0);
	Weltall::FMovieSubtitlePicture Line;
	Line.X = 8;
	Line.Y = 8;
	Line.Width = 16;
	Line.Height = 16;
	Line.Pixels.assign(16 * 16 * 4, 0);
	for (int32 Pixel = 0; Pixel < 16 * 16; ++Pixel)
	{
		const bool bWhite = Pixel < 16 * 8;
		const uint8 Colour = bWhite ? 255 : 0;
		Line.Pixels[Pixel * 4] = Colour;
		Line.Pixels[Pixel * 4 + 1] = Colour;
		Line.Pixels[Pixel * 4 + 2] = Colour;
		Line.Pixels[Pixel * 4 + 3] = bWhite ? 255 : 128;
	}
	for (const Weltall::EMovieFilter Filter : {Weltall::EMovieFilter::Smooth, Weltall::EMovieFilter::Upscaled})
	{
		const TCHAR* FilterName = Filter == Weltall::EMovieFilter::Smooth ? TEXT("Smooth") : TEXT("Upscaled");
		TSharedPtr<FMovieSubtitleTexture> Subtitle = MakeShared<FMovieSubtitleTexture>(Line, FIntPoint(32, 32));
		BeginInitResource(Subtitle.Get());
		FMoviePictureTexture Texture(Picture, Filter, Subtitle);
		BeginInitResource(&Texture);
		const FIntPoint Size(32, 32);
		const TArray<FColor> Pixels = RenderMovieOffscreen(Texture, Size, {32, 32}, {0, 0, 32, 32});
		BeginReleaseResource(&Texture);
		BeginReleaseResource(Subtitle.Get());
		FlushRenderingCommands();
		if (!TestEqual(FString::Printf(TEXT("%s: the frame was read back"), FilterName), Pixels.Num(), Size.X * Size.Y))
		{
			return false;
		}
		const auto Near = [this, &Pixels, Size, FilterName](const TCHAR* What, int32 X, int32 Y, int32 Grey)
		{
			const FColor& Pixel = Pixels[Y * Size.X + X];
			const bool bNear = FMath::Abs(Pixel.R - Grey) <= 2 && FMath::Abs(Pixel.G - Grey) <= 2 && FMath::Abs(Pixel.B - Grey) <= 2;
			TestTrue(FString::Printf(TEXT("%s, %s: (%d, %d, %d) near %d"), FilterName, What, Pixel.R, Pixel.G, Pixel.B, Grey), bNear);
		};
		Near(TEXT("the picture beside the line"), 2, 2, 160);
		Near(TEXT("the picture right of the line"), 28, 12, 160);
		Near(TEXT("the line's opaque white"), 16, 10, 255);
		// 160 x (1 - 128 / 255).
		Near(TEXT("the line's half-clear black"), 16, 20, 80);
	}
	return true;
}

#endif
