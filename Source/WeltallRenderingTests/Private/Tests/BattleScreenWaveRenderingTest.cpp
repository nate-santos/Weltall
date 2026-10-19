#include "Battle/Show/BattleShowScreenWave.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/BattleScreenWaveRendering.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 WaveScreenWidth = 320;
	constexpr int32 WaveScreenHeight = 224;

	/** One displayed frame of the wave at Position, over a picture of one colour with a mark in it. */
	TArray<FColor> RenderWaveFrame(FBattleScreenWaveBuffers& Buffers, float Position, bool bBlack = false)
	{
		const FIntPoint Size(2 * WaveScreenWidth, WaveScreenHeight);
		FBattleScreenWaveDraw Draw;
		Draw.Position = Position;
		Draw.bBlack = bBlack;
		return RenderPassesOffscreen(Size, [&Buffers, &Draw, Size](FRDGBuilder& GraphBuilder, FRDGTextureRef Output)
		{
			FRDGTextureRef Picture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("WaveTestPicture"));
			AddClearRenderTargetPass(GraphBuilder, Picture, FColor(200, 120, 60).ReinterpretAsLinear());
			AddClearRenderTargetPass(GraphBuilder, Picture, FColor(20, 40, 240).ReinterpretAsLinear(), FIntRect(280, 80, 360, 144));
			Buffers.AddPasses(GraphBuilder, Picture, Draw, Output, FIntRect(FIntPoint::ZeroValue, Size));
		});
	}

	int32 LargestDifference(const TArray<FColor>& A, const TArray<FColor>& B)
	{
		int32 Largest = 0;
		for (int32 Index = 0; Index < FMath::Min(A.Num(), B.Num()); ++Index)
		{
			const int32 Channel = FMath::Max3(FMath::Abs(A[Index].R - B[Index].R), FMath::Abs(A[Index].G - B[Index].G), FMath::Abs(A[Index].B - B[Index].B));
			Largest = FMath::Max(Largest, Channel);
		}
		return Largest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleScreenWavePlaceTest, "Weltall.Rendering.BattleScreenWaveIsShownABlankBehindItsFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBattleScreenWavePlaceTest::RunTest(const FString&)
{
	using Weltall::FBattleScreenWave;
	FBattleScreenWave Wave;
	Wave.Start();
	TestEqual(TEXT("Its first blank shows the picture"), BattleScreenWaveDraw(Wave, 1.0f).Position, 0.0f);
	for (int32 Blank = 0; Blank < 21; ++Blank)
	{
		Wave.Blank();
	}
	// Frame 10 steps at blank 20 and is shown whole at the end of blank 21, half a frame on from frame 9.5 at its start.
	TestEqual(TEXT("A blank after its step, a frame is shown whole"), BattleScreenWaveDraw(Wave, 1.0f).Position, 10.0f);
	TestEqual(TEXT("Between two blanks, half a frame back"), BattleScreenWaveDraw(Wave, 0.0f).Position, 9.5f);
	while (Wave.BlanksPassed() < FBattleScreenWave::BlackFrom)
	{
		TestFalse(TEXT("Black only once the screen is cleared"), BattleScreenWaveDraw(Wave, 1.0f).bBlack);
		Wave.Blank();
	}
	TestTrue(TEXT("Black once the screen is cleared"), BattleScreenWaveDraw(Wave, 0.0f).bBlack);
	TestEqual(TEXT("Its last frame stays"), BattleScreenWaveDraw(Wave, 0.0f).Position, static_cast<float>(FBattleScreenWave::FrameCount));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleScreenWaveRenderingTest, "Weltall.Rendering.BattleScreenWaveDrawsEveryFrameIntoItsBuffersOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBattleScreenWaveRenderingTest::RunTest(const FString&)
{
	constexpr float Late = 140.0f;
	// The first frame shows the picture as it is.
	TSharedRef<FBattleScreenWaveBuffers> Stepped = FBattleScreenWaveBuffers::Make();
	const TArray<FColor> First = RenderWaveFrame(*Stepped, 0.0f);
	if (!TestEqual(TEXT("The frame was read back"), First.Num(), 2 * WaveScreenWidth * WaveScreenHeight))
	{
		return false;
	}
	const FColor& Middle = First[100 * 2 * WaveScreenWidth + 320];
	TestTrue(FString::Printf(TEXT("The picture before the first frame: (%d, %d, %d)"), Middle.R, Middle.G, Middle.B),
		FMath::Abs(Middle.R - 20) <= 2 && FMath::Abs(Middle.G - 40) <= 2 && FMath::Abs(Middle.B - 240) <= 2);
	const FColor& Wing = First[10 * 2 * WaveScreenWidth + 10];
	TestTrue(FString::Printf(TEXT("And past the screen's side: (%d, %d, %d)"), Wing.R, Wing.G, Wing.B),
		FMath::Abs(Wing.R - 200) <= 2 && FMath::Abs(Wing.G - 120) <= 2 && FMath::Abs(Wing.B - 60) <= 2);

	// Shown a frame at a time or only late, the buffers have taken the same frames.
	TArray<FColor> Each;
	for (float Position = 1.0f; Position <= Late; Position += 1.0f)
	{
		Each = RenderWaveFrame(*Stepped, Position);
	}
	TSharedRef<FBattleScreenWaveBuffers> Skipped = FBattleScreenWaveBuffers::Make();
	const TArray<FColor> Once = RenderWaveFrame(*Skipped, Late);
	TestTrue(FString::Printf(TEXT("The frames the display skipped are in the buffers (largest difference %d)"), LargestDifference(Each, Once)),
		LargestDifference(Each, Once) <= 1);
	TestTrue(TEXT("The wave has moved the picture"), LargestDifference(First, Once) > 40);
	// A frame drawn again changes nothing, nor does an earlier one.
	TestTrue(TEXT("The same frame again"), LargestDifference(RenderWaveFrame(*Skipped, Late), Once) <= 1);
	TestTrue(TEXT("An earlier frame shows the latest"), LargestDifference(RenderWaveFrame(*Skipped, 20.0f), Once) <= 1);

	// By its last frame every triangle is black, and the trails are gone.
	const TArray<FColor> Last = RenderWaveFrame(*Skipped, static_cast<float>(Weltall::FBattleScreenWave::FrameCount));
	int32 Brightest = 0;
	for (const FColor& Pixel : Last)
	{
		Brightest = FMath::Max(Brightest, FMath::Max3(static_cast<int32>(Pixel.R), static_cast<int32>(Pixel.G), static_cast<int32>(Pixel.B)));
	}
	TestTrue(FString::Printf(TEXT("Black by its last frame (brightest %d)"), Brightest), Brightest <= 2);
	const TArray<FColor> Cleared = RenderWaveFrame(*Skipped, static_cast<float>(Weltall::FBattleScreenWave::FrameCount), true);
	TestTrue(TEXT("The cleared screen is black"), Cleared.Num() > 0 && Cleared[0] == FColor(0, 0, 0, Cleared[0].A));
	FlushRenderingCommands();
	return true;
}

#endif
