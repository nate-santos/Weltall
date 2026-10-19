#include "Rendering/MovieRendering.h"

#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "Graphics/Mdec.h"
#include "Graphics/MovieCleanup.h"
#include "Graphics/MovieUpscale.h"
#include "Media/MovieSubtitles.h"
#include "PixelShaderUtils.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "RHITextureInitializer.h"
#include "RenderGraphBuilder.h"
#include "ShaderParameterStruct.h"

namespace
{
	class FScreenMoviePS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FScreenMoviePS);
		SHADER_USE_PARAMETER_STRUCT(FScreenMoviePS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_TEXTURE(Texture2D, Luma)
			SHADER_PARAMETER_TEXTURE(Texture2D, Chroma)
			SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Subtitle)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearClamp)
			SHADER_PARAMETER(FVector4f, ScreenRect)
			SHADER_PARAMETER(FVector2f, ScreenSize)
			SHADER_PARAMETER(FVector4f, Placement)
			SHADER_PARAMETER(FVector2f, PictureSize)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	};

	/** What the upscaler's steps read (MovieUpscale.usf); each step uses its share. */
	BEGIN_SHADER_PARAMETER_STRUCT(FMovieUpscaleParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D<int>, Luma)
		SHADER_PARAMETER_TEXTURE(Texture2D<int4>, Chroma)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<int4>, Image)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<int>, Strength)
		SHADER_PARAMETER(FIntPoint, ImageSize)
		SHADER_PARAMETER(int32, NoiseFloor)
		SHADER_PARAMETER(int32, ChromaNoise)
		SHADER_PARAMETER(FIntPoint, Direction)
		SHADER_PARAMETER(FIntVector4, DisplayRect)
		SHADER_PARAMETER(FIntPoint, ScreenSize)
		SHADER_PARAMETER(FIntVector4, Placement)
		SHADER_PARAMETER_TEXTURE(Texture2D<float4>, Subtitle)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubtitleSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

#define MOVIE_UPSCALE_SHADER(Name)                                    \
	class Name final : public FGlobalShader                           \
	{                                                                 \
	public:                                                           \
		using FParameters = FMovieUpscaleParameters;                  \
		DECLARE_GLOBAL_SHADER(Name);                                  \
		SHADER_USE_PARAMETER_STRUCT(Name, FGlobalShader);             \
	};

	MOVIE_UPSCALE_SHADER(FMovieGuidePS)
	MOVIE_UPSCALE_SHADER(FMovieKeepColourPS)
	MOVIE_UPSCALE_SHADER(FMovieDiagonalsPS)
	MOVIE_UPSCALE_SHADER(FMovieRestPS)
	MOVIE_UPSCALE_SHADER(FMovieEdgesPS)
	MOVIE_UPSCALE_SHADER(FMovieBlurPS)
	MOVIE_UPSCALE_SHADER(FMovieWarpPS)
	MOVIE_UPSCALE_SHADER(FMovieDisplayPS)

#undef MOVIE_UPSCALE_SHADER

	/** Samples in 8-bit steps: the decoder keeps MdecSampleFractionBits more. */
	constexpr float StepsPerSample = 1.0f / (1 << Weltall::MdecSampleFractionBits);

	FTextureRHIRef CreatePlane(FRHICommandListBase& RHICmdList, const TCHAR* Name, FIntPoint Size, EPixelFormat Format, int32 TexelBytes, const void* Texels)
	{
		const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(Name, Size, Format)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask)
			.SetInitActionInitializer();
		FRHITextureInitializer Initializer = RHICmdList.CreateTextureInitializer(Desc);
		const FRHITextureSubresourceInitializer Mip = Initializer.GetTexture2DSubresource(0);
		const int32 RowBytes = Size.X * TexelBytes;
		for (int32 Row = 0; Row < Size.Y; ++Row)
		{
			FMemory::Memcpy(static_cast<uint8*>(Mip.Data) + Row * Mip.Stride, static_cast<const uint8*>(Texels) + Row * RowBytes, RowBytes);
		}
		return Initializer.Finalize();
	}

	FRDGTextureRef CreateTarget(FRDGBuilder& GraphBuilder, FIntPoint Size, EPixelFormat Format, const TCHAR* Name)
	{
		return GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(Size, Format, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource), Name);
	}

	/** Draws one step of the upscaler over the whole of Output. */
	template <typename TShader>
	void AddUpscaleStep(FRDGBuilder& GraphBuilder, FMovieUpscaleParameters* Parameters, FRDGTextureRef Output)
	{
		Parameters->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ENoAction);
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		const TShaderMapRef<TShader> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("MovieUpscale %s", Output->Name), PixelShader, Parameters,
			FIntRect(FIntPoint::ZeroValue, Output->Desc.Extent));
	}
}

IMPLEMENT_GLOBAL_SHADER(FScreenMoviePS, "/Weltall/Private/ScreenMovie.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieGuidePS, "/Weltall/Private/MovieUpscale.usf", "GuidePS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieKeepColourPS, "/Weltall/Private/MovieUpscale.usf", "KeepColourPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieDiagonalsPS, "/Weltall/Private/MovieUpscale.usf", "DiagonalsPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieRestPS, "/Weltall/Private/MovieUpscale.usf", "RestPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieEdgesPS, "/Weltall/Private/MovieUpscale.usf", "EdgesPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieBlurPS, "/Weltall/Private/MovieUpscale.usf", "BlurPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieWarpPS, "/Weltall/Private/MovieUpscale.usf", "WarpPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FMovieDisplayPS, "/Weltall/Private/MovieUpscale.usf", "DisplayPS", SF_Pixel);

FMovieSubtitleTexture::FMovieSubtitleTexture(const Weltall::FMovieSubtitlePicture& Line, FIntPoint PictureSize)
	: Size(PictureSize)
{
	Texels.SetNumZeroed(Size.X * Size.Y * 4);
	const int32 Left = FMath::Max(Line.X, 0);
	const int32 Right = FMath::Min(Line.X + Line.Width, Size.X);
	for (int32 Row = FMath::Max(Line.Y, 0); Row < FMath::Min(Line.Y + Line.Height, Size.Y) && Left < Right; ++Row)
	{
		FMemory::Memcpy(&Texels[(Row * Size.X + Left) * 4], &Line.Pixels[static_cast<std::size_t>(((Row - Line.Y) * Line.Width + Left - Line.X) * 4)],
			(Right - Left) * 4);
	}
}

void FMovieSubtitleTexture::InitRHI(FRHICommandListBase& RHICmdList)
{
	SubtitleTexture = CreatePlane(RHICmdList, TEXT("MovieSubtitle"), Size, PF_R8G8B8A8, 4, Texels.GetData());
}

void FMovieSubtitleTexture::ReleaseRHI()
{
	SubtitleTexture.SafeRelease();
}

FMoviePictureTexture::FMoviePictureTexture(const Weltall::FMdecPicture& Picture, Weltall::EMovieFilter InFilter, TSharedPtr<FMovieSubtitleTexture> InSubtitle)
	: PictureSize(Picture.Width, Picture.Height), PictureFilter(InFilter), SubtitleLine(MoveTemp(InSubtitle)), QuantScale(Picture.QuantScale)
{
	if (PictureFilter == Weltall::EMovieFilter::Upscaled)
	{
		Cleaning = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Cleaned = Picture]() mutable
		{
			Weltall::CleanMoviePicture(Cleaned);
			FSamples Samples;
			Samples.Luma = TArray<int16>(Cleaned.Luma.data(), static_cast<int32>(Cleaned.Luma.size()));
			Samples.Chroma.SetNumZeroed(static_cast<int32>(Cleaned.ChromaBlue.size() * 4));
			for (std::size_t Index = 0; Index < Cleaned.ChromaBlue.size(); ++Index)
			{
				Samples.Chroma[static_cast<int32>(Index * 4)] = Cleaned.ChromaBlue[Index];
				Samples.Chroma[static_cast<int32>(Index * 4 + 1)] = Cleaned.ChromaRed[Index];
			}
			return Samples;
		});
		return;
	}
	LumaTexels.SetNumUninitialized(static_cast<int32>(Picture.Luma.size()));
	for (int32 Index = 0; Index < LumaTexels.Num(); ++Index)
	{
		LumaTexels[Index] = Picture.Luma[static_cast<std::size_t>(Index)] * StepsPerSample;
	}
	ChromaTexels.SetNumUninitialized(static_cast<int32>(Picture.ChromaBlue.size() * 2));
	for (std::size_t Index = 0; Index < Picture.ChromaBlue.size(); ++Index)
	{
		ChromaTexels[static_cast<int32>(Index * 2)] = Picture.ChromaBlue[Index] * StepsPerSample;
		ChromaTexels[static_cast<int32>(Index * 2 + 1)] = Picture.ChromaRed[Index] * StepsPerSample;
	}
}

void FMoviePictureTexture::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (PictureFilter == Weltall::EMovieFilter::Upscaled)
	{
		const FSamples& Samples = Cleaning.GetResult();
		LumaTexture = CreatePlane(RHICmdList, TEXT("MovieLumaSamples"), PictureSize, PF_R16_SINT, sizeof(int16), Samples.Luma.GetData());
		ChromaTexture = CreatePlane(RHICmdList, TEXT("MovieChromaSamples"), PictureSize / 2, PF_R16G16B16A16_SINT, 4 * sizeof(int16), Samples.Chroma.GetData());
		return;
	}
	LumaTexture = CreatePlane(RHICmdList, TEXT("MovieLuma"), PictureSize, PF_R32_FLOAT, sizeof(float), LumaTexels.GetData());
	ChromaTexture = CreatePlane(RHICmdList, TEXT("MovieChroma"), PictureSize / 2, PF_G32R32F, 2 * sizeof(float), ChromaTexels.GetData());
}

FRHITexture* FMoviePictureTexture::Subtitle() const
{
	return SubtitleLine.IsValid() && SubtitleLine->Texture() != nullptr ? SubtitleLine->Texture() : GTransparentBlackTexture->TextureRHI.GetReference();
}

void FMoviePictureTexture::ReleaseRHI()
{
	LumaTexture.SafeRelease();
	ChromaTexture.SafeRelease();
	UpscaledImage.SafeRelease();
}

FRDGTextureRef FMoviePictureTexture::Upscaled(FRDGBuilder& GraphBuilder) const
{
	if (UpscaledImage.IsValid())
	{
		return GraphBuilder.RegisterExternalTexture(UpscaledImage);
	}
	const int32 NoiseFloor = Weltall::MovieUpscale::NoiseFloor(QuantScale);
	FMovieUpscaleParameters* Guide = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
	Guide->Luma = LumaTexture;
	Guide->Chroma = ChromaTexture;
	Guide->ImageSize = PictureSize;
	FRDGTextureRef Guided = CreateTarget(GraphBuilder, PictureSize, PF_R16G16B16A16_SINT, TEXT("MovieGuided"));
	AddUpscaleStep<FMovieGuidePS>(GraphBuilder, Guide, Guided);
	FMovieUpscaleParameters* Keep = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
	Keep->Chroma = ChromaTexture;
	Keep->Image = Guided;
	Keep->ChromaNoise = Weltall::MovieUpscale::ChromaNoise(QuantScale);
	FRDGTextureRef Image = CreateTarget(GraphBuilder, PictureSize, PF_R16G16B16A16_SINT, TEXT("MovieColourKept"));
	AddUpscaleStep<FMovieKeepColourPS>(GraphBuilder, Keep, Image);
	for (int32 Doubling = 1; Doubling < Weltall::MovieUpscale::Factor; Doubling *= 2)
	{
		const FIntPoint From = Image->Desc.Extent;
		FMovieUpscaleParameters* Diagonals = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
		Diagonals->Image = Image;
		Diagonals->ImageSize = From;
		Diagonals->NoiseFloor = NoiseFloor;
		FRDGTextureRef Between = CreateTarget(GraphBuilder, From * 2, PF_R16G16B16A16_SINT, TEXT("MovieDiagonals"));
		AddUpscaleStep<FMovieDiagonalsPS>(GraphBuilder, Diagonals, Between);
		FMovieUpscaleParameters* Rest = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
		Rest->Image = Between;
		Rest->ImageSize = From * 2;
		Rest->NoiseFloor = NoiseFloor;
		Image = CreateTarget(GraphBuilder, From * 2, PF_R16G16B16A16_SINT, TEXT("MovieDoubled"));
		AddUpscaleStep<FMovieRestPS>(GraphBuilder, Rest, Image);
	}
	const FIntPoint Size = Image->Desc.Extent;
	FMovieUpscaleParameters* Edges = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
	Edges->Image = Image;
	Edges->ImageSize = Size;
	FRDGTextureRef Strength = CreateTarget(GraphBuilder, Size, PF_R16_SINT, TEXT("MovieEdges"));
	AddUpscaleStep<FMovieEdgesPS>(GraphBuilder, Edges, Strength);
	for (const FIntPoint Direction : {FIntPoint(1, 0), FIntPoint(0, 1)})
	{
		FMovieUpscaleParameters* Blur = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
		Blur->Strength = Strength;
		Blur->ImageSize = Size;
		Blur->Direction = Direction;
		Strength = CreateTarget(GraphBuilder, Size, PF_R16_SINT, TEXT("MovieEdgesBlurred"));
		AddUpscaleStep<FMovieBlurPS>(GraphBuilder, Blur, Strength);
	}
	FMovieUpscaleParameters* Warp = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
	Warp->Image = Image;
	Warp->Strength = Strength;
	Warp->ImageSize = Size;
	FRDGTextureRef Sharpened = CreateTarget(GraphBuilder, Size, PF_R16G16B16A16_SINT, TEXT("MovieUpscaled"));
	AddUpscaleStep<FMovieWarpPS>(GraphBuilder, Warp, Sharpened);
	GraphBuilder.QueueTextureExtraction(Sharpened, &UpscaledImage);
	return Sharpened;
}

void AddMoviePasses(FRDGBuilder& GraphBuilder, const FMoviePictureTexture& Picture, FRDGTextureRef Output, const FIntRect& ScreenRect,
	Weltall::FScreenSize Screen, const Weltall::FScreenRect& Placement)
{
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	if (Picture.Filter() == Weltall::EMovieFilter::Upscaled)
	{
		FRDGTextureRef Upscaled = Picture.Upscaled(GraphBuilder);
		FMovieUpscaleParameters* Parameters = GraphBuilder.AllocParameters<FMovieUpscaleParameters>();
		Parameters->Image = Upscaled;
		Parameters->ImageSize = Upscaled->Desc.Extent;
		Parameters->DisplayRect = FIntVector4(ScreenRect.Min.X, ScreenRect.Min.Y, ScreenRect.Width(), ScreenRect.Height());
		Parameters->ScreenSize = FIntPoint(Screen.Width, Screen.Height);
		Parameters->Placement = FIntVector4(Placement.X, Placement.Y, Placement.Width, Placement.Height);
		Parameters->Subtitle = Picture.Subtitle();
		Parameters->SubtitleSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ELoad);
		const TShaderMapRef<FMovieDisplayPS> PixelShader(ShaderMap);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("ScreenMovieUpscaled"), PixelShader, Parameters, ScreenRect);
		return;
	}
	FScreenMoviePS::FParameters* Parameters = GraphBuilder.AllocParameters<FScreenMoviePS::FParameters>();
	Parameters->Luma = Picture.Luma();
	Parameters->Chroma = Picture.Chroma();
	Parameters->Subtitle = Picture.Subtitle();
	Parameters->LinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ScreenRect = FVector4f(ScreenRect.Min.X, ScreenRect.Min.Y, ScreenRect.Width(), ScreenRect.Height());
	Parameters->ScreenSize = FVector2f(Screen.Width, Screen.Height);
	Parameters->Placement = FVector4f(Placement.X, Placement.Y, Placement.Width, Placement.Height);
	Parameters->PictureSize = FVector2f(Picture.Size().X, Picture.Size().Y);
	Parameters->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ELoad);
	const TShaderMapRef<FScreenMoviePS> PixelShader(ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("ScreenMovie"), PixelShader, Parameters, ScreenRect);
}
