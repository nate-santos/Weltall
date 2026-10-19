#include "FrameProfile.h"

#include "DynamicRHI.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "Screens/GameScreen.h"

DEFINE_LOG_CATEGORY_STATIC(LogFrameProfile, Log, All);

namespace
{
	/** The mean, median, 99th percentile and worst of times in seconds, in milliseconds. */
	FString Distribution(TArray<double> Seconds)
	{
		if (Seconds.IsEmpty())
		{
			return TEXT("none");
		}
		Seconds.Sort();
		double Sum = 0.0;
		for (const double Value : Seconds)
		{
			Sum += Value;
		}
		const auto At = [&Seconds](double Fraction) { return Seconds[FMath::Min(Seconds.Num() - 1, static_cast<int32>(Fraction * Seconds.Num()))] * 1000.0; };
		return FString::Printf(TEXT("mean %.3f, median %.3f, 99%% %.3f, worst %.3f ms"), Sum / Seconds.Num() * 1000.0, At(0.5), At(0.99), Seconds.Last() * 1000.0);
	}
}

FFrameProfile::FFrameProfile(FIntPoint InSize, const Weltall::FDisplayOptions& InOptions, double InFrom)
	: Size(InSize), Options(InOptions), From(InFrom)
{
}

FFrameProfile::~FFrameProfile()
{
	// The queries are the rendering thread's.
	ENQUEUE_RENDER_COMMAND(ReleaseFrameProfile)([Released = MoveTemp(Began), Also = MoveTemp(Ended), Picture = MoveTemp(Display)](FRHICommandListImmediate&) {});
	FlushRenderingCommands();
}

void FFrameProfile::Draw(const IScreenFrame& Frame, double Update, double Capture, double At)
{
	double PassSeconds = 0.0;
	double GpuSeconds = 0.0;
	ENQUEUE_RENDER_COMMAND(ProfiledFrame)([this, &Frame, &PassSeconds, &GpuSeconds](FRHICommandListImmediate& RHICmdList)
	{
		if (!Began.IsValid())
		{
			Began = RHICreateRenderQuery(RQT_AbsoluteTime);
			Ended = RHICreateRenderQuery(RQT_AbsoluteTime);
		}
		RHICmdList.EndRenderQuery(Began);
		const double Start = FPlatformTime::Seconds();
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			// The display's picture outlives the frame, as a window's back buffer does, so none of the passes drawing it are culled.
			FRDGTextureRef Output = Display.IsValid() ? GraphBuilder.RegisterExternalTexture(Display) : GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("ProfiledDisplay"));
			AddDisplayFramePasses(GraphBuilder, Output, FIntRect(FIntPoint::ZeroValue, Size), Options, &Frame);
			if (!Display.IsValid())
			{
				GraphBuilder.QueueTextureExtraction(Output, &Display);
			}
			GraphBuilder.SetTextureAccessFinal(Output, ERHIAccess::RTV);
			GraphBuilder.Execute();
		}
		PassSeconds = FPlatformTime::Seconds() - Start;
		RHICmdList.EndRenderQuery(Ended);
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		uint64 BeganMicroseconds = 0;
		uint64 EndedMicroseconds = 0;
		if (RHIGetRenderQueryResult(Began, BeganMicroseconds, true) && RHIGetRenderQueryResult(Ended, EndedMicroseconds, true))
		{
			GpuSeconds = static_cast<double>(EndedMicroseconds - BeganMicroseconds) / 1.0e6;
		}
		// The frame ends as the game's do: the pools let go of what lay unused, and the RHI recycles its frame's memory.
		GRenderTargetPool.TickPoolElements();
		FRDGBuilder::TickPoolElements();
		RHICmdList.EndFrame();
	});
	FlushRenderingCommands();
	if (At < From)
	{
		return;
	}
	Updates.Add(Update);
	Captures.Add(Capture);
	Passes.Add(PassSeconds);
	Gpu.Add(GpuSeconds);
	Moments.Add(At);
}

void FFrameProfile::Log(double DisplayRate) const
{
	UE_LOG(LogFrameProfile, Display, TEXT("%d frames at %.0f Hz, %dx%d"), Updates.Num(), DisplayRate, Size.X, Size.Y);
	UE_LOG(LogFrameProfile, Display, TEXT("  update (game thread):    %s"), *Distribution(Updates));
	UE_LOG(LogFrameProfile, Display, TEXT("  capture (game thread):   %s"), *Distribution(Captures));
	UE_LOG(LogFrameProfile, Display, TEXT("  passes (render thread):  %s"), *Distribution(Passes));
	UE_LOG(LogFrameProfile, Display, TEXT("  drawing (GPU):           %s"), *Distribution(Gpu));
	TArray<int32> Costliest;
	for (int32 Index = 0; Index < Updates.Num(); ++Index)
	{
		Costliest.Add(Index);
	}
	const auto Total = [this](int32 Index) { return Updates[Index] + Captures[Index] + Passes[Index] + Gpu[Index]; };
	Costliest.Sort([&Total](int32 A, int32 B) { return Total(A) > Total(B); });
	for (int32 Rank = 0; Rank < FMath::Min(Costliest.Num(), 5); ++Rank)
	{
		const int32 Index = Costliest[Rank];
		UE_LOG(LogFrameProfile, Display, TEXT("  costly at %.3f s: update %.3f, capture %.3f, passes %.3f, GPU %.3f ms"), Moments[Index], Updates[Index] * 1000.0,
			Captures[Index] * 1000.0, Passes[Index] * 1000.0, Gpu[Index] * 1000.0);
	}
}
