#pragma once

#include "CoreMinimal.h"
#include "Field/FieldRenderOptions.h"
#include "Presentation/GameScreen.h"
#include "RenderGraphFwd.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RHIResources.h"
#include "Tasks/Task.h"

#include <memory>

namespace Weltall
{
	struct FMdecPicture;
	struct FMovieSubtitlePicture;
}

/** A subtitle line on the GPU: a texture as large as the movie's picture, clear around the line, its colour premultiplied. */
class WELTALL_API FMovieSubtitleTexture final : public FRenderResource
{
public:
	/** Places the line in a picture of PictureSize, on the calling thread; the GPU texture is made by InitRHI. */
	FMovieSubtitleTexture(const Weltall::FMovieSubtitlePicture& Line, FIntPoint PictureSize);

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	FRHITexture* Texture() const { return SubtitleTexture; }

private:
	FIntPoint Size;
	TArray<uint8> Texels;
	FTextureRHIRef SubtitleTexture;
};

/**
 * A decoded movie picture on the GPU, as its filter takes it. Smooth: its luma plane, and its two chroma
 * planes at half resolution, as floats in 8-bit steps (luma centred on zero), filterable for scaling.
 * Upscaled: the picture cleaned of its compression's traces on a worker thread, as the decoder's integers,
 * and the upscaled image made from it on the GPU the first time the picture is drawn. Either way, the subtitle line
 * shown with it, if any, is laid over it as it is drawn.
 */
class WELTALL_API FMoviePictureTexture final : public FRenderResource
{
public:
	/** Converts the planes, Smooth's on the calling thread; the GPU textures are made by InitResource. */
	explicit FMoviePictureTexture(const Weltall::FMdecPicture& Picture, Weltall::EMovieFilter InFilter = Weltall::EMovieFilter::Smooth,
		TSharedPtr<FMovieSubtitleTexture> InSubtitle = nullptr);

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	Weltall::EMovieFilter Filter() const { return PictureFilter; }
	FRHITexture* Luma() const { return LumaTexture; }
	FRHITexture* Chroma() const { return ChromaTexture; }
	FIntPoint Size() const { return PictureSize; }
	/** The subtitle laid over the picture; a clear texture when there is none. Rendering thread. */
	FRHITexture* Subtitle() const;

	/** The upscaled image (Upscaled's filter), its passes added the first time it is asked for. Rendering thread. */
	FRDGTextureRef Upscaled(FRDGBuilder& GraphBuilder) const;

private:
	/** Upscaled's planes as uploaded: the luma, and Cb, Cr and two zeros per texel. */
	struct FSamples
	{
		TArray<int16> Luma;
		TArray<int16> Chroma;
	};

	FIntPoint PictureSize;
	Weltall::EMovieFilter PictureFilter;
	TSharedPtr<FMovieSubtitleTexture> SubtitleLine;
	int32 QuantScale = 0;
	/** Smooth's planes: the luma, and Cb and Cr interleaved per texel. */
	TArray<float> LumaTexels;
	TArray<float> ChromaTexels;
	/** Upscaled's planes, cleaned as the picture arrives; InitRHI waits for them. */
	UE::Tasks::TTask<FSamples> Cleaning;
	FTextureRHIRef LumaTexture;
	FTextureRHIRef ChromaTexture;
	mutable TRefCountPtr<IPooledRenderTarget> UpscaledImage;
};

/**
 * Draws a movie picture over Output: the game screen (Screen of its own pixels) fills ScreenRect,
 * and the picture's top-left Shown pixels land at Placement in it; the rest stays as it is.
 */
WELTALL_API void AddMoviePasses(FRDGBuilder& GraphBuilder, const FMoviePictureTexture& Picture, FRDGTextureRef Output, const FIntRect& ScreenRect,
	Weltall::FScreenSize Screen, const Weltall::FScreenRect& Placement);
