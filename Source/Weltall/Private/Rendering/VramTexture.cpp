#include "Rendering/VramTexture.h"
#include "Rendering/TextureModLibrary.h"
#include "TextureReplacementGpu.h"

#include "GlobalShader.h"
#include "HAL/IConsoleManager.h"
#include "Graphics/Texture.h"
#include "Graphics/TextureRegion.h"
#include "Graphics/TextureUpscale.h"
#include "PixelShaderUtils.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"

#include <bit>

CSV_DEFINE_CATEGORY(WeltallVram, true);

namespace
{
	TAutoConsoleVariable<int32> CVarPartialVramUploads(TEXT("weltall.PartialVramUploads"), 1,
		TEXT("Use bounded uploads when console tiles change and port rows are unchanged. Zero retains full uploads for performance comparisons."));

	class FTextureDoublePS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FTextureDoublePS);
		SHADER_USE_PARAMETER_STRUCT(FTextureDoublePS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, Vram)
			SHADER_PARAMETER_TEXTURE(Texture2D<uint>, VramEdges)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, Doubled)
			SHADER_PARAMETER(uint32, TexelsPerUnit)
			SHADER_PARAMETER(uint32, Scale)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	};

	constexpr Weltall::ETextureDepth UpscaledDepths[2] = {Weltall::ETextureDepth::Indexed4, Weltall::ETextureDepth::Indexed8};

	uint32 TexelsPerUnit(Weltall::ETextureDepth Depth)
	{
		return Depth == Weltall::ETextureDepth::Indexed4 ? 4 : 2;
	}

	/** The upscaled texels of one depth at Scale times VRAM's reading, kept from before or made now (the caller keeps them once drawn). */
	FRDGTextureRef UpscaledTarget(FRDGBuilder& GraphBuilder, const TRefCountPtr<IPooledRenderTarget>& Kept, Weltall::ETextureDepth Depth, int32 Scale, const TCHAR* Name)
	{
		if (Kept.IsValid())
		{
			return GraphBuilder.RegisterExternalTexture(Kept);
		}
		const FIntPoint Size(Weltall::FVram::Width * static_cast<int32>(TexelsPerUnit(Depth)) * Scale, Weltall::FVram::Height * Scale);
		return GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, PF_R8_UINT, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource), Name);
	}

	/** Doubles From (VRAM's units at Scale 1, the first doubling's texels at 2) over Rects into Into. */
	void AddDoublings(FRDGBuilder& GraphBuilder, FRDGTextureRef Units, FRHITexture* Edges, FRDGTextureRef From, FRDGTextureRef Into,
		Weltall::ETextureDepth Depth, int32 Scale, const std::vector<Weltall::FVramRect>& Rects)
	{
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		const TShaderMapRef<FTextureDoublePS> PixelShader(ShaderMap);
		for (const Weltall::FVramRect& Rect : Rects)
		{
			const Weltall::TextureUpscale::FTexelRect Texels = Weltall::TextureUpscale::TexelsOver(Rect, Depth, Scale * 2);
			FTextureDoublePS::FParameters* Parameters = GraphBuilder.AllocParameters<FTextureDoublePS::FParameters>();
			Parameters->Vram = Units;
			Parameters->VramEdges = Edges;
			Parameters->Doubled = From;
			Parameters->TexelsPerUnit = TexelsPerUnit(Depth);
			Parameters->Scale = static_cast<uint32>(Scale);
			Parameters->RenderTargets[0] = FRenderTargetBinding(Into, ERenderTargetLoadAction::ELoad);
			FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("TextureUpscale %s", Into->Name), PixelShader, Parameters,
				FIntRect(Texels.X, Texels.Y, Texels.X + Texels.Width, Texels.Y + Texels.Height));
		}
	}
}

IMPLEMENT_GLOBAL_SHADER(FTextureDoublePS, "/Weltall/Private/TextureUpscale.usf", "DoublePS", SF_Pixel);

uint32 FVramTexture::Pack(const Weltall::FTexturePage& Page, const Weltall::FClutPosition& Clut)
{
	return Weltall::PackTextureView(Page, Clut);
}

void FVramTexture::SetUpscaled(bool bUpscaled)
{
	if (bUpscaled == bUpscaledOnGameThread)
	{
		return;
	}
	bUpscaledOnGameThread = bUpscaled;
	bSendAll = bUpscaled;
	if (!bUpscaled)
	{
		ENQUEUE_RENDER_COMMAND(StopUpscalingVram)([this](FRHICommandListImmediate&)
		{
			bEdgesSent = false;
			for (int32 Depth = 0; Depth < 2; ++Depth)
			{
				DoubledTexels[Depth].SafeRelease();
				UpscaledTexels[Depth].SafeRelease();
			}
		});
	}
}

bool FVramTexture::Follow(const Weltall::FVram& Vram, std::span<const Weltall::FTextureRegion> Regions, std::span<const Weltall::FTexturePictureSeed> Seeds)
{
	FollowReplacements(Vram, Regions, Seeds);
	return Follow(Vram);
}

bool FVramTexture::Follow(const Weltall::FVram& Vram)
{
	if (!FTextureModLibrary::IsActive()) FollowReplacements(Vram, {}, {});
	const bool bSameLineage = bSent && Vram.Lineage() == SentLineage;
	if (bSameLineage && Vram.Revision() == SentRevision && Vram.PortRevision() == SentPortRevision && !bSendAll)
	{
		return false;
	}
	TSharedRef<FCopy>* Free = Staging.FindByPredicate([](const TSharedRef<FCopy>& Candidate) { return Candidate.IsUnique(); });
	CSV_SCOPED_TIMING_STAT(WeltallVram, StageCopy);
	TSharedRef<FCopy> Copy = Free != nullptr ? *Free : Staging.Add_GetRef(MakeShared<FCopy>());
	if (bSameLineage && Vram.Revision() > SentRevision && !bSendAll)
	{
		Copy->Changed = Vram.TilesChangedSince(SentRevision);
	}
	else
	{
		Copy->Changed = Weltall::FVram::EveryTile();
	}
	Copy->UploadArea = FIntRect(0, 0, Weltall::FVram::Width, Weltall::FVram::Rows);
	if (bSameLineage && Vram.Revision() >= SentRevision && Vram.PortRevision() == SentPortRevision && !bSendAll && CVarPartialVramUploads.GetValueOnGameThread() != 0)
	{
		// The mask covers only console rows. Any port-row change takes the full path above.
		FIntPoint Min(Weltall::FVram::Width, Weltall::FVram::Height);
		FIntPoint Max(0, 0);
		for (int32 Row = 0; Row < Weltall::FVram::TilesDown; ++Row)
		{
			const uint64 Bits = Copy->Changed[static_cast<std::size_t>(Row)];
			if (Bits == 0) continue;
			Min.X = FMath::Min(Min.X, static_cast<int32>(std::countr_zero(Bits)) * Weltall::FVram::TileWidth);
			Min.Y = FMath::Min(Min.Y, Row * Weltall::FVram::TileHeight);
			Max.X = FMath::Max(Max.X, (64 - static_cast<int32>(std::countl_zero(Bits))) * Weltall::FVram::TileWidth);
			Max.Y = (Row + 1) * Weltall::FVram::TileHeight;
		}
		Copy->UploadArea = Max.X > Min.X ? FIntRect(Min, Max) : FIntRect(0, 0, 0, 0);
	}
	bSent = true;
	bSendAll = false;
	SentRevision = Vram.Revision();
	SentPortRevision = Vram.PortRevision();
	SentLineage = Vram.Lineage();
	const std::span<const uint16> Source = Vram.AllUnits();
	Copy->Units.SetNumUninitialized(static_cast<int32>(Source.size()), EAllowShrinking::No);
	FMemory::Memcpy(Copy->Units.GetData(), Source.data(), Source.size() * sizeof(uint16));
	if (bUpscaledOnGameThread)
	{
		const std::span<const uint8> SourceEdges = Vram.AllEdges();
		Copy->Edges.SetNumUninitialized(static_cast<int32>(SourceEdges.size()), EAllowShrinking::No);
		FMemory::Memcpy(Copy->Edges.GetData(), SourceEdges.data(), SourceEdges.size());
	}
	else
	{
		Copy->Edges.Reset();
	}
	ENQUEUE_RENDER_COMMAND(FollowVram)([this, Copy](FRHICommandListImmediate& RHICmdList)
	{
		CSV_SCOPED_TIMING_STAT(WeltallVram, SubmitUpload);
		const FIntRect Area = Copy->UploadArea;
		const bool bUpload = Area.Width() > 0 && Area.Height() > 0;
		CSV_CUSTOM_STAT(WeltallVram, Updates, bUpload ? 1 : 0, ECsvCustomStatOp::Accumulate);
		CSV_CUSTOM_STAT(WeltallVram, QueuedTextureBytes, static_cast<double>(Area.Width()) * Area.Height() * (sizeof(uint16) + (Copy->Edges.IsEmpty() ? 0 : 1)), ECsvCustomStatOp::Accumulate);
#if CSV_PROFILER
		int32 ChangedTiles = 0;
		for (uint64 Row : Copy->Changed)
		{
			ChangedTiles += std::popcount(Row);
		}
		CSV_CUSTOM_STAT(WeltallVram, ChangedTiles, ChangedTiles, ECsvCustomStatOp::Accumulate);
#endif
		const FUpdateTextureRegion2D Region(Area.Min.X, Area.Min.Y, 0, 0, Area.Width(), Area.Height());
		const int32 Offset = Area.Min.Y * Weltall::FVram::Width + Area.Min.X;
		if (bUpload)
		{
			RHICmdList.UpdateTexture2D(Units, 0, Region, Weltall::FVram::Width * sizeof(uint16), reinterpret_cast<const uint8*>(Copy->Units.GetData() + Offset));
		}
		if (bUpload && !Copy->Edges.IsEmpty())
		{
			RHICmdList.UpdateTexture2D(Edges, 0, Region, Weltall::FVram::Width, Copy->Edges.GetData() + Offset);
			bEdgesSent = true;
		}
		for (std::size_t Row = 0; Row < Pending.size(); ++Row)
		{
			Pending[Row] |= Copy->Changed[Row];
		}
	});
	return true;
}

void FVramTexture::InitRHI(FRHICommandListBase& RHICmdList)
{
	ReplacementGpu->InitRHI(RHICmdList);
	const FIntPoint Size(Weltall::FVram::Width, Weltall::FVram::Rows);
	Units = RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(TEXT("PlayStationVram"), Size, PF_R16_UINT)
		.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable)
		.SetInitialState(ERHIAccess::SRVMask));
	Edges = RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(TEXT("PlayStationVramEdges"), Size, PF_R8_UINT)
		.SetFlags(ETextureCreateFlags::ShaderResource)
		.SetInitialState(ERHIAccess::SRVMask));
}

void FVramTexture::ReleaseRHI()
{
	Units.SafeRelease();
	Edges.SafeRelease();
	ReplacementGpu->ReleaseRHI();
	for (int32 Depth = 0; Depth < 2; ++Depth)
	{
		DoubledTexels[Depth].SafeRelease();
		UpscaledTexels[Depth].SafeRelease();
	}
}

FVramTexture::FUpscaled FVramTexture::Upscaled(FRDGBuilder& GraphBuilder) const
{
	if (!bEdgesSent)
	{
		return {};
	}
	if (!UpscaledTexels[0].IsValid())
	{
		Pending = Weltall::FVram::EveryTile();
	}
	const std::vector<Weltall::FVramRect> Rects = Weltall::TextureUpscale::RectsToRedo(Pending);
	Pending = {};
	const FRDGTextureRef VramUnits = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Units, TEXT("PlayStationVram")));
	FRDGTextureRef Made[2] = {};
	for (int32 Depth = 0; Depth < 2; ++Depth)
	{
		const Weltall::ETextureDepth Reading = UpscaledDepths[Depth];
		const bool bNew = !UpscaledTexels[Depth].IsValid();
		const FRDGTextureRef Doubled = UpscaledTarget(GraphBuilder, DoubledTexels[Depth], Reading, 2, TEXT("VramDoubledTexels"));
		Made[Depth] = UpscaledTarget(GraphBuilder, UpscaledTexels[Depth], Reading, Weltall::TextureUpscale::Factor, TEXT("VramUpscaledTexels"));
		if (!Rects.empty())
		{
			AddDoublings(GraphBuilder, VramUnits, Edges, VramUnits, Doubled, Reading, 1, Rects);
			AddDoublings(GraphBuilder, VramUnits, Edges, Doubled, Made[Depth], Reading, 2, Rects);
		}
		if (bNew)
		{
			GraphBuilder.QueueTextureExtraction(Doubled, &DoubledTexels[Depth]);
			GraphBuilder.QueueTextureExtraction(Made[Depth], &UpscaledTexels[Depth]);
		}
	}
	return {Made[0], Made[1]};
}
