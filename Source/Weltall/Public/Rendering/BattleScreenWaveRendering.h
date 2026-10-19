#pragma once

#include "Battle/Show/BattleShowScreenWave.h"
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "RendererInterface.h"
#include "Rendering/BattleTransitionRendering.h"

/** Where a displayed frame of the screen wave stands: in its frames, between two, or black once it has cleared the screen. */
struct FBattleScreenWaveDraw
{
	/** Frame n is the wave's latest from its 2n-th vertical blank; shown from a blank later, so that it moves at every blank. */
	float Position = 0.0f;
	bool bBlack = false;
};

/** The wave's place between its last vertical blank and its next, Alpha of the way. */
WELTALL_API FBattleScreenWaveDraw BattleScreenWaveDraw(const Weltall::FBattleScreenWave& Wave, float Alpha);

/**
 * The screen wave's two draw buffers, which it never clears, at the output's size: each starts as the picture of the
 * screen before the wave, and each frame is averaged over what its buffer held two frames before. A displayed frame
 * shows the buffer of its frame with that frame's triangles over it, Alpha of the way from those of the frame before.
 * Kept on the rendering thread from one displayed frame to the next, which draws into the buffers every frame they
 * have not taken yet, in order (another output or area replays them from the picture).
 */
class WELTALL_API FBattleScreenWaveBuffers
{
public:
	/** Made on the game thread, released on the rendering thread. */
	static TSharedRef<FBattleScreenWaveBuffers> Make();

	/** Adds the passes that draw Draw into Area of Output, textured from Picture (the screen before the wave at the area's size; none: black). */
	void AddPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, const FBattleScreenWaveDraw& Draw, FRDGTextureRef Output, const FIntRect& Area);

private:
	FBattleScreenWaveBuffers();

	/** The triangles of a frame, Along of the way from those of the frame before; the frames are taken in order. */
	void Take(int32 Frame, float Along);

	FBattleTransitionDrawBuilder Triangles;
	FBattleTransitionDraw Taken;
	/** The wave's mesh stepped as the wave steps it, to each frame taken. */
	Weltall::FBattleScreenMesh Mesh;
	TRefCountPtr<IPooledRenderTarget> Buffers[2];
	FIntRect BuffersArea;
	/** The latest frame drawn into the buffers, and the furthest place shown: a frame drawn again shows no earlier one. */
	int32 Drawn = 0;
	float Furthest = 0.0f;
};
