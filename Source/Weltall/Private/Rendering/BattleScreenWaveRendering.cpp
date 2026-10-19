#include "Rendering/BattleScreenWaveRendering.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/FieldPictureRendering.h"

namespace
{
	/** Picture over the whole of Area of Target, as it is. */
	void AddWholePicture(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, FRDGTextureRef Target, const FIntRect& Area)
	{
		if (Picture == nullptr)
		{
			return;
		}
		const FVector4f Corners[4] = {FVector4f(0.0f, 0.0f, 0.0f, 0.0f), FVector4f(1.0f, 0.0f, 1.0f, 0.0f), FVector4f(0.0f, 1.0f, 0.0f, 1.0f), FVector4f(1.0f, 1.0f, 1.0f, 1.0f)};
		TArray<FVector4f, TInlineAllocator<6>> Quad;
		for (const int32 Corner : {0, 1, 2, 1, 3, 2})
		{
			Quad.Add(Corners[Corner]);
		}
		FFieldPictureDraw Drawn;
		Drawn.Vertices = Quad;
		Drawn.Placement = FVector4f(static_cast<float>(Area.Width()), static_cast<float>(Area.Height()), static_cast<float>(Area.Min.X), static_cast<float>(Area.Min.Y));
		Drawn.bScissor = true;
		Drawn.Scissor = Area;
		AddFieldPicturePass(GraphBuilder, Picture, Target, Drawn);
	}
}

FBattleScreenWaveDraw BattleScreenWaveDraw(const Weltall::FBattleScreenWave& Wave, float Alpha)
{
	using Weltall::FBattleScreenWave;
	FBattleScreenWaveDraw Draw;
	Draw.bBlack = Wave.IsBlack();
	const float Blanks = static_cast<float>(Wave.BlanksPassed() - 2) + FMath::Clamp(Alpha, 0.0f, 1.0f);
	Draw.Position = FMath::Clamp(Blanks * 0.5f, 0.0f, static_cast<float>(FBattleScreenWave::FrameCount));
	return Draw;
}

TSharedRef<FBattleScreenWaveBuffers> FBattleScreenWaveBuffers::Make()
{
	return TSharedRef<FBattleScreenWaveBuffers>(new FBattleScreenWaveBuffers(), [](FBattleScreenWaveBuffers* Released)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseBattleScreenWaveBuffers)([Released](FRHICommandListImmediate&) { delete Released; });
	});
}

FBattleScreenWaveBuffers::FBattleScreenWaveBuffers()
{
	Mesh.Create(Weltall::EBattleScreenMesh::ScreenWave);
}

void FBattleScreenWaveBuffers::Take(int32 Frame, float Along)
{
	while (Mesh.Steps() < Frame)
	{
		Mesh.Step((Mesh.Steps() + 1) & 1);
	}
	Triangles.Build(Mesh, Frame & 1, Frame, Along, Taken);
}

void FBattleScreenWaveBuffers::AddPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Picture, const FBattleScreenWaveDraw& Draw, FRDGTextureRef Output,
	const FIntRect& Area)
{
	if (Area.Width() <= 0 || Area.Height() <= 0)
	{
		return;
	}
	if (Draw.bBlack)
	{
		AddClearRenderTargetPass(GraphBuilder, Output, FLinearColor::Black, Area);
		return;
	}
	const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Output->Desc.Extent, Output->Desc.Format, FClearValueBinding::Black,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	const bool bFresh = !Buffers[0].IsValid() || Buffers[0]->GetDesc().Extent != Desc.Extent || Buffers[0]->GetDesc().Format != Desc.Format
		|| BuffersArea != Area;
	FRDGTextureRef Textures[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (bFresh)
		{
			Textures[Index] = GraphBuilder.CreateTexture(Desc, TEXT("BattleScreenWave"));
			AddClearRenderTargetPass(GraphBuilder, Textures[Index], FLinearColor::Black);
			AddWholePicture(GraphBuilder, Picture, Textures[Index], Area);
			GraphBuilder.QueueTextureExtraction(Textures[Index], &Buffers[Index]);
		}
		else
		{
			Textures[Index] = GraphBuilder.RegisterExternalTexture(Buffers[Index]);
		}
	}
	if (bFresh)
	{
		// The frames drawn so far are drawn again from the first.
		BuffersArea = Area;
		Drawn = 0;
		Mesh.Create(Weltall::EBattleScreenMesh::ScreenWave);
		Triangles.Reset();
	}
	Furthest = FMath::Max(Furthest, Draw.Position);
	const int32 Shown = FMath::CeilToInt32(Furthest);
	// Each frame before the shown one once, into its own buffer over what that held two frames before.
	while (Drawn < Shown - 1)
	{
		++Drawn;
		Take(Drawn, 1.0f);
		AddBattleTransitionTriangles(GraphBuilder, Picture, Taken, Textures[Drawn & 1], Area);
	}
	// The shown frame's buffer holds the frame two before it, both buffers the picture before the first two.
	FRHICopyTextureInfo Copy;
	Copy.SourcePosition = FIntVector(Area.Min.X, Area.Min.Y, 0);
	Copy.DestPosition = Copy.SourcePosition;
	Copy.Size = FIntVector(Area.Width(), Area.Height(), 1);
	AddCopyTexturePass(GraphBuilder, Textures[(Shown - 2) & 1], Output, Copy);
	Take(Shown, Furthest - static_cast<float>(Shown - 1));
	AddBattleTransitionTriangles(GraphBuilder, Picture, Taken, Output, Area);
}
