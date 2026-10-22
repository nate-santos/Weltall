#include "Presentation/WeltallViewportClient.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/TouchPad.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/ScaledFrameRendering.h"
#include "Screens/GameScreen.h"
#include "Screens/GameScreenSubsystem.h"
#include "Shell/PlayerSettingsSubsystem.h"
#include "UnrealClient.h"

#if PLATFORM_ANDROID
namespace
{
	TAutoConsoleVariable<float> CVarMobilePictureScale(TEXT("weltall.MobilePictureScale"), 1.0f,
		TEXT("Diagnostic picture resolution scale, 0.5 to 1. Native touch UI is unchanged. Default 1 keeps the original path."),
		ECVF_RenderThreadSafe);
}
#endif

void UWeltallViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
	Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
	bDisableWorldRendering = true;
}

void UWeltallViewportClient::Draw(FViewport* InViewport, FCanvas* SceneCanvas)
{
	// Without world rendering this only clears the viewport and queues the engine's own overlays,
	// which are drawn after the screen.
	Super::Draw(InViewport, SceneCanvas);

	const UGameInstance* Game = GetGameInstance();
	const UGameScreenSubsystem* Screens = Game != nullptr ? Game->GetSubsystem<UGameScreenSubsystem>() : nullptr;
	TSharedPtr<const IScreenFrame> Frame = Screens != nullptr ? Screens->CaptureFrame() : nullptr;
	const UPlayerSettingsSubsystem* Settings = Game != nullptr ? Game->GetSubsystem<UPlayerSettingsSubsystem>() : nullptr;
	const Weltall::FDisplayOptions Options = Settings != nullptr ? Settings->Settings().Display : Weltall::FDisplayOptions{};
	const FIntPoint Size = InViewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		// Android can briefly have no drawable surface while rotating or resuming.
		return;
	}
	FIntRect Display(FIntPoint::ZeroValue, Size);
#if PLATFORM_ANDROID
	// Match Slate's safe area. Both the notch side and navigation insets may change after a flip.
	FMargin Safe;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetSafeZoneSize(Safe, FVector2D(Size));
	}
	const FIntPoint SafeMin(FMath::RoundToInt(Safe.Left), FMath::RoundToInt(Safe.Top));
	const FIntPoint SafeSize(FMath::Max(1, Size.X - SafeMin.X - FMath::RoundToInt(Safe.Right)),
		FMath::Max(1, Size.Y - SafeMin.Y - FMath::RoundToInt(Safe.Bottom)));
	const Weltall::FTouchOptions Touch = Settings != nullptr ? Settings->Settings().Touch : Weltall::FTouchOptions{};
	const Weltall::FTouchRect Picture = Weltall::FTouchLayout::Build(static_cast<float>(SafeSize.X), static_cast<float>(SafeSize.Y), Touch).Picture;
	const FIntPoint PictureMin = SafeMin + FIntPoint(FMath::RoundToInt(Picture.X), FMath::RoundToInt(Picture.Y));
	Display = FIntRect(PictureMin, PictureMin + FIntPoint(FMath::RoundToInt(Picture.Width), FMath::RoundToInt(Picture.Height)));
#endif
	ENQUEUE_RENDER_COMMAND(DrawGameScreen)([InViewport, Frame = MoveTemp(Frame), Size, Display, Options](FRHICommandListImmediate& RHICmdList)
	{
		FRHITexture* Target = InViewport->GetRenderTargetTexture();
		if (Target == nullptr)
		{
			return;
		}
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef Output = RegisterExternalTexture(GraphBuilder, Target, TEXT("GameViewport"));
		GraphBuilder.SetTextureAccessFinal(Output, ERHIAccess::RTV);
		const FIntPoint Extent = Size.ComponentMin(Output->Desc.Extent);
		const FIntRect Picture(Display.Min.ComponentMin(Extent), Display.Max.ComponentMin(Extent));
		if (Picture != FIntRect(FIntPoint::ZeroValue, Extent))
		{
			AddClearRenderTargetPass(GraphBuilder, Output, FLinearColor::Black);
		}
#if PLATFORM_ANDROID
		AddScaledDisplayFramePasses(GraphBuilder, Output, Picture, Options, Frame.Get(), CVarMobilePictureScale.GetValueOnRenderThread());
#else
		AddDisplayFramePasses(GraphBuilder, Output, Picture, Options, Frame.Get());
#endif
		GraphBuilder.Execute();
	});
}
