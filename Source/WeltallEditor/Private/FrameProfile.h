#pragma once

#include "CoreMinimal.h"
#include "Presentation/DisplayOptions.h"
#include "RenderGraphResources.h"
#include "RHIResources.h"

class IScreenFrame;

/**
 * What displayed frames of a screen cost: the game thread's update and capture of each, the rendering thread's
 * passes, and the GPU's drawing of it into a target of the display's size. Each frame is drawn alone, the next
 * begun once the GPU has finished it, so no part's time includes waiting on another's. Every frame is drawn, as
 * the game draws them, and those from a moment of play on are timed.
 */
class FFrameProfile
{
public:
	/** GpuPassesAt, when not negative, is the moment of play whose frame the engine's ProfileGPU breaks down by pass in the log. */
	FFrameProfile(FIntPoint InSize, const Weltall::FDisplayOptions& InOptions, double InFrom, double InGpuPassesAt = -1.0);
	~FFrameProfile();

	/** Draws a captured frame and, from the moment timed on, notes its times; Update and Capture are its game thread's, in seconds, At its moment of play. */
	void Draw(const IScreenFrame& Frame, double Update, double Capture, double At);

	/** Logs each part's mean, median, 99th percentile and worst over the frames added, and the costliest frames. */
	void Log(double DisplayRate) const;

private:
	FIntPoint Size;
	Weltall::FDisplayOptions Options;
	double From = 0.0;
	double GpuPassesAt = -1.0;
	TArray<double> Updates;
	TArray<double> Captures;
	TArray<double> Passes;
	TArray<double> Gpu;
	TArray<double> Moments;
	TRefCountPtr<IPooledRenderTarget> Display;
	FRenderQueryRHIRef Began;
	FRenderQueryRHIRef Ended;
};
