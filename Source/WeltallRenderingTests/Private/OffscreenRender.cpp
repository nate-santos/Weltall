#include "OffscreenRender.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/MovieRendering.h"

namespace
{
	/** Runs AddPasses into a new color target of the given size on the rendering thread and reads the target back. */
	template <typename TAddPasses>
	TArray<FColor> RenderOffscreen(FIntPoint Size, TAddPasses AddPasses)
	{
		TArray<FColor> Pixels;
		ENQUEUE_RENDER_COMMAND(OffscreenFrame)([&AddPasses, &Pixels, Size](FRHICommandListImmediate& RHICmdList)
		{
			TRefCountPtr<IPooledRenderTarget> Output;
			{
				FRDGBuilder GraphBuilder(RHICmdList);
				FRDGTextureRef Color = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource),
					TEXT("OffscreenColor"));
				AddPasses(GraphBuilder, Color);
				GraphBuilder.QueueTextureExtraction(Color, &Output);
				GraphBuilder.Execute();
			}
			RHICmdList.ReadSurfaceData(Output->GetRHI(), FIntRect(FIntPoint::ZeroValue, Size), Pixels, FReadSurfaceDataFlags());
		});
		FlushRenderingCommands();
		return Pixels;
	}
}

TArray<FColor> RenderDrawListOffscreen(const FDrawListTextures& Textures, const FDrawListFrame& Frame, FIntPoint Size, Weltall::FScreenSize Screen)
{
	return RenderOffscreen(Size, [&Textures, &Frame, Size, Screen](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		const FIntRect Area(FIntPoint::ZeroValue, Size);
		AddClearRenderTargetPass(GraphBuilder, Color, FLinearColor::Black, Area);
		AddDrawListPasses(GraphBuilder, Textures, Frame, Color, Area, Screen);
	});
}

TArray<FColor> RenderMovieOffscreen(const FMoviePictureTexture& Picture, FIntPoint Size, Weltall::FScreenSize Screen, const Weltall::FScreenRect& Placement)
{
	return RenderOffscreen(Size, [&Picture, Size, Screen, &Placement](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		const FIntRect Area(FIntPoint::ZeroValue, Size);
		AddClearRenderTargetPass(GraphBuilder, Color, FLinearColor::Black, Area);
		AddMoviePasses(GraphBuilder, Picture, Color, Area, Screen, Placement);
	});
}

TArray<FColor> RenderFieldSceneOffscreen(const FFieldSceneMesh& Mesh, const FVramTexture& Vram, const FFieldSceneDraw& Draw, FIntPoint Size)
{
	return RenderOffscreen(Size, [&Mesh, &Vram, &Draw, Size](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		const FIntRect Area(FIntPoint::ZeroValue, Size);
		AddClearRenderTargetPass(GraphBuilder, Color, FLinearColor::Black, Area);
		AddFieldScenePasses(GraphBuilder, Mesh, Vram, Draw, Color, Area);
	});
}

TArray<FColor> RenderPassesOffscreen(FIntPoint Size, TFunctionRef<void(FRDGBuilder&, FRDGTextureRef)> AddPasses)
{
	return RenderOffscreen(Size, [&AddPasses, Size](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddClearRenderTargetPass(GraphBuilder, Color, FLinearColor::Black, FIntRect(FIntPoint::ZeroValue, Size));
		AddPasses(GraphBuilder, Color);
	});
}

TArray<FColor> RenderScreenFrameOffscreen(const IScreenFrame& Frame, FIntPoint Size, const Weltall::FDisplayOptions& Options)
{
	return RenderOffscreen(Size, [&Frame, Size, &Options](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDisplayFramePasses(GraphBuilder, Color, FIntRect(FIntPoint::ZeroValue, Size), Options, &Frame);
	});
}
