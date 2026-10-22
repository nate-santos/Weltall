#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "Presentation/DisplayOptions.h"

class IScreenFrame;

/** Draw the game picture at a lower resolution, then enlarge it; Slate controls stay native.
 * Scale 1 uses the original passes directly. Diagnostic only until device comparisons qualify presets.
 */
WELTALL_API void AddScaledDisplayFramePasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FIntRect& Display,
	const Weltall::FDisplayOptions& Options, const IScreenFrame* Frame, float Scale);
