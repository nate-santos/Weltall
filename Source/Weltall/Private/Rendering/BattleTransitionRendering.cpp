#include "Rendering/BattleTransitionRendering.h"

#include "Graphics/SpriteDrawing.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/FieldSceneRendering.h"

namespace
{
	/** A colour that leaves the picture's texels as they are. */
	constexpr float TransitionNeutralColor = 128.0f;
	/** A corner this near the eye or behind it has no place on the screen. */
	constexpr float TransitionNearestCorner = 1.0f;

	/** Where the transitions' own projection puts a corner on the screen; false when it has no place there. */
	bool ProjectTransitionCorner(const Weltall::FGteMatrix& Matrix, const Weltall::FGteVector& Corner, FVector2f& Screen)
	{
		using Weltall::FBattleShatter;
		const std::array<float, 3> View = Weltall::ViewPoint(Matrix, Corner);
		const float Scale = static_cast<float>(FBattleShatter::ProjectionDistance) / FMath::Max(View[2], TransitionNearestCorner);
		Screen = FVector2f(View[0] * Scale + FBattleShatter::ScreenCentreX, View[1] * Scale + FBattleShatter::ScreenCentreY);
		return View[2] >= TransitionNearestCorner;
	}
}

void FBattleTransitionDrawBuilder::Reset()
{
	LatestFrame = INDEX_NONE;
	bPrevious = false;
}

bool FBattleTransitionDrawBuilder::BeginFrame(int32 Frame)
{
	if (Frame == LatestFrame)
	{
		return false;
	}
	bPrevious = LatestFrame != INDEX_NONE && Frame == LatestFrame + 1;
	Swap(Latest, Previous);
	Backgrounds[0] = Backgrounds[1];
	LatestFrame = Frame;
	return true;
}

void FBattleTransitionDrawBuilder::Take(const Weltall::FBattleDefaultTransition& Transition)
{
	using Weltall::FBattleShatter;
	using Weltall::FBattleShatterWings;
	const std::size_t Buffer = static_cast<std::size_t>(Transition.Buffer());
	Latest.Reset();
	// In the order the transition links them: side by side, row by row, column by column.
	for (int32 Side = 0; Side < FBattleShatter::Sides; ++Side)
	{
		for (int32 Row = 0; Row < FBattleShatter::Rows; ++Row)
		{
			for (int32 Column = FBattleShatterWings::FirstColumn; Column < FBattleShatterWings::EndColumn; ++Column)
			{
				const bool bWing = Column < 0 || Column >= FBattleShatter::Columns;
				const Weltall::FBattleScreenShard& Shard = bWing ? Transition.Wings().Shard(Side, Row, Column) : Transition.Shatter().Shard(Side, Row, Column);
				FTriangle& Drawn = Latest.AddDefaulted_GetRef();
				const std::array<std::array<int32, 2>, 3> Pixels = FBattleShatter::PicturePixels(Side, Row, Column);
				const Weltall::FGteMatrix Matrix = FBattleShatter::DrawMatrix(Shard);
				bool bOnScreen = Shard.Position[2] > FBattleShatter::NearestDrawn;
				for (std::size_t Corner = 0; Corner < 3; ++Corner)
				{
					bOnScreen &= ProjectTransitionCorner(Matrix, FBattleShatter::Corners[static_cast<std::size_t>(Side)][Corner], Drawn.Corners[Corner]);
					Drawn.Pixels[Corner] = FVector2f(Pixels[Corner][0], Pixels[Corner][1]);
					Drawn.Shades[Corner] = static_cast<float>(Shard.Colors[Buffer][0]) / TransitionNeutralColor;
				}
				if (bOnScreen)
				{
					Drawn.Entry = FBattleShatter::DrawEntry(Shard, Side);
				}
			}
		}
	}
}

void FBattleTransitionDrawBuilder::Take(const Weltall::FBattleScreenMesh& Mesh, int32 Buffer)
{
	Mesh.Draw(Buffer, -Mesh.WingColumns(), Mesh.Columns() + Mesh.WingColumns(), MeshTriangles);
	Latest.Reset();
	for (const Weltall::FBattleMeshTriangle& Triangle : MeshTriangles)
	{
		FTriangle& Drawn = Latest.AddDefaulted_GetRef();
		const Weltall::FBattleMeshLink Link = Mesh.Link(Triangle);
		bool bOnScreen = Link.bLinked;
		for (std::size_t Corner = 0; Corner < 3; ++Corner)
		{
			bOnScreen &= ProjectTransitionCorner(Triangle.Matrix, Triangle.Corners[Corner], Drawn.Corners[Corner]);
			Drawn.Pixels[Corner] = FVector2f(Triangle.Pixels[Corner][0], Triangle.Pixels[Corner][1]);
			Drawn.Shades[Corner] = static_cast<float>(Triangle.Colors[Corner]) / TransitionNeutralColor;
		}
		if (bOnScreen)
		{
			Drawn.Entry = Link.Entry;
		}
	}
}

void FBattleTransitionDrawBuilder::Finish(float Alpha, FBattleTransitionDraw& Draw)
{
	if (bNewFrame)
	{
		// The far entries first; within an entry, the triangle linked last.
		Order.Reset(Latest.Num());
		for (int32 Index = Latest.Num() - 1; Index >= 0; --Index)
		{
			if (Latest[Index].Entry != INDEX_NONE)
			{
				Order.Add(Index);
			}
		}
		Order.StableSort([this](int32 A, int32 B) { return Latest[A].Entry > Latest[B].Entry; });
	}
	const bool bBlended = bPrevious && Previous.Num() == Latest.Num();
	const float Along = bBlended ? FMath::Clamp(Alpha, 0.0f, 1.0f) : 1.0f;
	const float Background = FMath::Lerp(Backgrounds[0], Backgrounds[1], Along);
	Draw.Background = FLinearColor(Background, Background, Background);
	Draw.Vertices.Reset(Order.Num() * 3);
	Draw.Shades.Reset(Order.Num() * 3);
	for (const int32 Index : Order)
	{
		const FTriangle& Now = Latest[Index];
		// A triangle the frame before did not draw starts where this one has it.
		const FTriangle& Before = bBlended && Previous[Index].Entry != INDEX_NONE ? Previous[Index] : Now;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const FVector2f Place = FMath::Lerp(Before.Corners[Corner], Now.Corners[Corner], Along);
			Draw.Vertices.Emplace(Place.X, Place.Y, Now.Pixels[Corner].X, Now.Pixels[Corner].Y);
			Draw.Shades.Add(FMath::Lerp(Before.Shades[Corner], Now.Shades[Corner], Along));
		}
	}
}

void FBattleTransitionDrawBuilder::Build(const Weltall::FBattleDefaultTransition& Transition, int32 Frame, float Alpha, FBattleTransitionDraw& Draw)
{
	bNewFrame = BeginFrame(Frame);
	if (bNewFrame)
	{
		Take(Transition);
		Backgrounds[1] = static_cast<float>(Transition.Backgrounds()[static_cast<std::size_t>(Transition.Buffer())][0]) / 255.0f;
	}
	// A shard keeps a black pixel of the picture: its texels never blend.
	Draw.Blend = EFieldPictureBlend::Opaque;
	Finish(Alpha, Draw);
}

void FBattleTransitionDrawBuilder::Build(const Weltall::FBattleScreenMesh& Mesh, int32 Buffer, int32 Frame, float Alpha, FBattleTransitionDraw& Draw)
{
	bNewFrame = BeginFrame(Frame);
	if (bNewFrame)
	{
		Take(Mesh, Buffer);
		Backgrounds[1] = 0.0f;
	}
	Draw.Blend = Mesh.Blend() == Weltall::EBlendMode::Add ? EFieldPictureBlend::Add : EFieldPictureBlend::Average;
	Finish(Alpha, Draw);
}

void AddBattleTransitionPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, const FBattleTransitionDraw& Draw, FRDGTextureRef Output, const FIntRect& Area)
{
	if (Area.Width() <= 0 || Area.Height() <= 0)
	{
		return;
	}
	AddClearRenderTargetPass(GraphBuilder, Output, Draw.Background, Area);
	AddBattleTransitionTriangles(GraphBuilder, Picture, Draw, Output, Area);
}

void AddBattleTransitionTriangles(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, const FBattleTransitionDraw& Draw, FRDGTextureRef Output, const FIntRect& Area)
{
	if (Picture == nullptr || Area.Width() <= 0 || Area.Height() <= 0)
	{
		return;
	}
	const FIntPoint Size = Area.Size();
	const FVector4f Placement = FieldScreenPlacement(Size);
	FFieldPictureDraw Triangles;
	Triangles.Vertices = Draw.Vertices;
	Triangles.Shades = Draw.Shades;
	Triangles.Blend = Draw.Blend;
	Triangles.Placement = Placement + FVector4f(0.0f, 0.0f, static_cast<float>(Area.Min.X), static_cast<float>(Area.Min.Y));
	Triangles.SourcePlacement = FVector4f(Placement.X / Size.X, Placement.Y / Size.Y, Placement.Z / Size.X, Placement.W / Size.Y);
	Triangles.bScissor = true;
	Triangles.Scissor = Area;
	AddFieldPicturePass(GraphBuilder, Picture, Output, Triangles);
}
