#include "Graphics/Mdec.h"
#include "Graphics/MovieCleanup.h"
#include "Graphics/MovieUpscale.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/MovieRendering.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovieUpscaleRenderingTest, "Weltall.Rendering.MovieUpscaledOnTheGpuIsTheCoresPictureBitForBit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/**
	 * A 64 x 48 picture with what the upscaler has to tell apart: a round edge, a diagonal one, a gradient, noise, colour
	 * on each, and a small red light whose colour the luma does not show.
	 */
	Weltall::FMdecPicture TestPicture()
	{
		Weltall::FMdecPicture Picture;
		Picture.Width = 64;
		Picture.Height = 48;
		Picture.QuantScale = 3;
		Picture.Luma.resize(64 * 48);
		Picture.ChromaBlue.resize(32 * 24);
		Picture.ChromaRed.resize(32 * 24);
		uint32 Noise = 12345;
		for (int32 Y = 0; Y < 48; ++Y)
		{
			for (int32 X = 0; X < 64; ++X)
			{
				Noise = Noise * 1664525u + 1013904223u;
				const bool bInDisc = (X - 20) * (X - 20) + (Y - 24) * (Y - 24) < 14 * 14;
				const bool bPastLine = X * 3 > Y * 2 + 100;
				const int32 Level = bInDisc ? 90 : (bPastLine ? -70 : (X + Y) - 40);
				Picture.Luma[Y * 64 + X] = static_cast<int16>(Level * 16 + static_cast<int32>(Noise >> 28) - 8);
			}
		}
		for (int32 Y = 0; Y < 24; ++Y)
		{
			for (int32 X = 0; X < 32; ++X)
			{
				const bool bInDisc = (2 * X - 20) * (2 * X - 20) + (2 * Y - 24) * (2 * Y - 24) < 14 * 14;
				Picture.ChromaBlue[Y * 32 + X] = static_cast<int16>(bInDisc ? -30 * 16 : 12 * 16);
				Picture.ChromaRed[Y * 32 + X] = static_cast<int16>(bInDisc ? 45 * 16 : (X > 20 ? -20 * 16 : 0));
			}
		}
		Picture.Luma[10 * 64 + 50] = 100 * 16;
		Picture.ChromaRed[5 * 32 + 25] = 60 * 16;
		return Picture;
	}
}

bool FMovieUpscaleRenderingTest::RunTest(const FString&)
{
	const Weltall::FMdecPicture Picture = TestPicture();
	Weltall::FMdecPicture Cleaned = Picture;
	Weltall::CleanMoviePicture(Cleaned);
	const Weltall::FMovieImage Upscaled = Weltall::MovieUpscale::Upscale(Cleaned);

	FMoviePictureTexture Texture(Picture, Weltall::EMovieFilter::Upscaled);
	BeginInitResource(&Texture);
	// The picture's top 40 lines shown 4 lines down a 64 x 48 screen, on a display of no whole multiple and on one of four times.
	for (const FIntPoint Size : {FIntPoint(250, 187), FIntPoint(256, 192)})
	{
		const TArray<FColor> Pixels = RenderMovieOffscreen(Texture, Size, {64, 48}, {0, 4, 64, 40});
		if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), Size.X * Size.Y))
		{
			break;
		}
		const Weltall::MovieUpscale::FPlacement Placement{Size.X, Size.Y, 64, 48, 0, 4, 64, 40};
		int32 Differing = 0;
		FString First;
		for (int32 Row = 0; Row < Size.Y; ++Row)
		{
			for (int32 Column = 0; Column < Size.X; ++Column)
			{
				std::array<uint8, 3> Rgb{};
				if (!Weltall::MovieUpscale::DisplayPixel(Upscaled, Placement, Column, Row, Rgb))
				{
					Rgb = {0, 0, 0};
				}
				const FColor& Pixel = Pixels[Row * Size.X + Column];
				if (Pixel.R != Rgb[0] || Pixel.G != Rgb[1] || Pixel.B != Rgb[2])
				{
					if (Differing++ == 0)
					{
						First = FString::Printf(TEXT("(%d, %d): GPU %d %d %d, core %d %d %d"), Column, Row, Pixel.R, Pixel.G, Pixel.B, Rgb[0], Rgb[1], Rgb[2]);
					}
				}
			}
		}
		TestEqual(FString::Printf(TEXT("Pixels unlike the core's at %dx%d, the first %s"), Size.X, Size.Y, *First), Differing, 0);
	}
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

#endif
