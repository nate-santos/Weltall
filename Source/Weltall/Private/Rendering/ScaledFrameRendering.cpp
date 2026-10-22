#include "Rendering/ScaledFrameRendering.h"

#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderGraphBuilder.h"
#include "RHIStaticStates.h"
#include "Screens/GameScreen.h"
#include "ShaderParameterStruct.h"

CSV_DEFINE_CATEGORY(WeltallPicture, true);

namespace
{
	class FScaledFramePS final : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FScaledFramePS);
		SHADER_USE_PARAMETER_STRUCT(FScaledFramePS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Picture)
			SHADER_PARAMETER_SAMPLER(SamplerState, LinearClamp)
			SHADER_PARAMETER(FVector2f, DisplayMin)
			SHADER_PARAMETER(FVector2f, InverseDisplaySize)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()
	};
}

IMPLEMENT_GLOBAL_SHADER(FScaledFramePS, "/Weltall/Private/ScaledFrame.usf", "MainPS", SF_Pixel);

void AddScaledDisplayFramePasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FIntRect& Display,
	const Weltall::FDisplayOptions& Options, const IScreenFrame* Frame, float Scale)
{
	if (Display.Width() <= 0 || Display.Height() <= 0)
	{
		return;
	}
	Scale = FMath::IsFinite(Scale) ? FMath::Clamp(Scale, 0.5f, 1.0f) : 1.0f;
	const FIntPoint Size(FMath::Max(1, FMath::RoundToInt(Display.Width() * Scale)),
		FMath::Max(1, FMath::RoundToInt(Display.Height() * Scale)));
	CSV_CUSTOM_STAT(WeltallPicture, Width, Size.X, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(WeltallPicture, Height, Size.Y, ECsvCustomStatOp::Set);
	if (Size == Display.Size())
	{
		AddDisplayFramePasses(GraphBuilder, Output, Display, Options, Frame);
		return;
	}
	FRDGTextureRef Picture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(Size, Output->Desc.Format,
		FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("ScaledGamePicture"));
	AddDisplayFramePasses(GraphBuilder, Picture, FIntRect(FIntPoint::ZeroValue, Size), Options, Frame);
	FScaledFramePS::FParameters* Parameters = GraphBuilder.AllocParameters<FScaledFramePS::FParameters>();
	Parameters->Picture = Picture;
	Parameters->LinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->DisplayMin = FVector2f(Display.Min.X, Display.Min.Y);
	Parameters->InverseDisplaySize = FVector2f(1.0f / Display.Width(), 1.0f / Display.Height());
	Parameters->RenderTargets[0] = FRenderTargetBinding(Output, ERenderTargetLoadAction::ELoad);
	const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	const TShaderMapRef<FScaledFramePS> PixelShader(ShaderMap);
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, ShaderMap, RDG_EVENT_NAME("ScaledGamePicture"), PixelShader, Parameters, Display);
}
