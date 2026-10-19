#pragma once

#include "Battle/Show/BattleShowTransition.h"
#include "CoreMinimal.h"
#include "RenderGraphFwd.h"
#include "Rendering/FieldPictureRendering.h"

#include <vector>

/**
 * The drawing of a frame of a battle's opening transition: the picture of the screen before the battle in triangles,
 * each showing its piece of the picture, over the frame's background, the triangles past the screen's sides included.
 */
struct FBattleTransitionDraw
{
	/** Per vertex, three a triangle, in the order they are drawn: where it lies on the 320 x 224 screen (xy) and the pixel of the picture it shows (zw). */
	TArray<FVector4f> Vertices;
	/** Per vertex, its modulation of the picture (a colour of 128 is 1). */
	TArray<float> Shades;
	/** How the triangles are drawn over the background and one another. */
	EFieldPictureBlend Blend = EFieldPictureBlend::Opaque;
	FLinearColor Background = FLinearColor::Black;
};

/**
 * Makes a transition's drawing at the display's rate: each triangle where the transition's latest frame put it, or
 * Alpha of the way there from the frame before's.
 */
class WELTALL_API FBattleTransitionDrawBuilder
{
public:
	/** Another transition begins: nothing of the last one is kept. */
	void Reset();

	/**
	 * The default transition's shards, opaque over a background that fades from white. Frame counts the transition's
	 * frames run, which tells a new frame from the one already seen. Reuses Draw's memory.
	 */
	void Build(const Weltall::FBattleDefaultTransition& Transition, int32 Frame, float Alpha, FBattleTransitionDraw& Draw);

	/** A setup transition's mesh as its frame drew it into Buffer, blended over black. */
	void Build(const Weltall::FBattleScreenMesh& Mesh, int32 Buffer, int32 Frame, float Alpha, FBattleTransitionDraw& Draw);

private:
	/** A triangle as a frame drew it. */
	struct FTriangle
	{
		FVector2f Corners[3];
		float Shades[3] = {};
		FVector2f Pixels[3];
		/** The ordering table's entry, none when it was not drawn. */
		int32 Entry = INDEX_NONE;
	};

	/** Whether Frame is a new one: then Previous holds the last and Latest waits to be filled. */
	bool BeginFrame(int32 Frame);
	void Take(const Weltall::FBattleDefaultTransition& Transition);
	void Take(const Weltall::FBattleScreenMesh& Mesh, int32 Buffer);
	/** The drawing order of Latest, then the drawing Alpha of the way from Previous. */
	void Finish(float Alpha, FBattleTransitionDraw& Draw);

	TArray<FTriangle> Latest;
	TArray<FTriangle> Previous;
	TArray<int32> Order;
	std::vector<Weltall::FBattleMeshTriangle> MeshTriangles;
	float Backgrounds[2] = {0.0f, 0.0f};
	int32 LatestFrame = INDEX_NONE;
	bool bPrevious = false;
	bool bNewFrame = false;
};

/**
 * Adds the passes that draw a transition into Area of Output: the background over the whole area, then the triangles,
 * textured from Picture (the screen's last picture as it filled an area of this size; none: a black one), the 320 x 224
 * screen filling the area's height at the centre of its width.
 */
WELTALL_API void AddBattleTransitionPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, const FBattleTransitionDraw& Draw, FRDGTextureRef Output,
	const FIntRect& Area);

/** Adds the pass that draws only the triangles of a transition's drawing into Area of Output, over what it holds there (as above). */
WELTALL_API void AddBattleTransitionTriangles(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, const FBattleTransitionDraw& Draw, FRDGTextureRef Output,
	const FIntRect& Area);
