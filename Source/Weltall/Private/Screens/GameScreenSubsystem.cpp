#include "Screens/GameScreenSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Input/ButtonGlyphSubsystem.h"
#include "Input/KeyListenerSubsystem.h"
#include "Input/PadPortsSubsystem.h"
#include "Input/WeltallPlayerController.h"
#include "Presentation/ScreenLayout.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Audio/GameSoundSubsystem.h"
#include "Screens/GameScreen.h"
#include "Shell/GameWindowSubsystem.h"

void AddDisplayFramePasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FIntRect& Display, Weltall::EDisplayAspect Aspect, const IScreenFrame* Frame)
{
	AddClearRenderTargetPass(GraphBuilder, Output, FLinearColor::Black, Display);
	if (Frame != nullptr)
	{
		const Weltall::FDisplayRect Picture = Weltall::FitAspect(Display.Width(), Display.Height(), Aspect);
		const FIntPoint Origin = Display.Min + FIntPoint(Picture.X, Picture.Y);
		Frame->AddPasses(GraphBuilder, Output, FIntRect(Origin, Origin + FIntPoint(Picture.Width, Picture.Height)));
	}
}

void UGameScreenSubsystem::Show(TSharedRef<IGameScreen> InScreen)
{
	Screen = MoveTemp(InScreen);
}

void UGameScreenSubsystem::Close()
{
	Screen.Reset();
	if (UPadPortsSubsystem* Ports = GetGameInstance()->GetSubsystem<UPadPortsSubsystem>())
	{
		Ports->SetSecondRead(false);
		Ports->SetMotors(false, false);
	}
}

TSharedPtr<const IScreenFrame> UGameScreenSubsystem::CaptureFrame() const
{
	return Screen ? Screen->CaptureFrame().ToSharedPtr() : nullptr;
}

void UGameScreenSubsystem::Deinitialize()
{
	// The game is shutting down: nothing runs or draws any more, even before this object is collected.
	Screen.Reset();
	Super::Deinitialize();
}

void UGameScreenSubsystem::Tick(float DeltaTime)
{
	// In the background, when the settings say so, the whole game stands still: no frame runs, no time passes, no sound is made.
	UGameWindowSubsystem* Window = GetGameInstance()->GetSubsystem<UGameWindowSubsystem>();
	AWeltallPlayerController* Controller = Cast<AWeltallPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
	const bool bHeld = (Window != nullptr && Window->IsHeldInBackground()) || (Controller != nullptr && Controller->IsTouchMenuOpen());
	// Keep audio paused too while discarding a delta that may include time spent suspended.
	const bool bResumeFrame = !bHeld && Window != nullptr && Window->ConsumeResumeFrame();
	if (UGameSoundSubsystem* Sound = GetGameInstance()->GetSubsystem<UGameSoundSubsystem>())
	{
		Sound->SetHeld(bHeld || bResumeFrame);
	}
	UPadPortsSubsystem* Ports = GetGameInstance()->GetSubsystem<UPadPortsSubsystem>();
	if (bHeld || bResumeFrame)
	{
		// A motor running as the game is held would run on.
		if (Ports != nullptr)
		{
			Ports->SetMotors(false, false);
		}
		return;
	}
	if (Controller != nullptr)
	{
		Screen->AddViewInput(Controller->ReadView(DeltaTime));
	}
	FGamePads Pads;
	if (const UButtonGlyphSubsystem* Glyphs = GetGameInstance()->GetSubsystem<UButtonGlyphSubsystem>())
	{
		Pads.bConnected = Glyphs->HasController() || (Controller != nullptr && Controller->HasTouchControls());
	}
	if (Ports != nullptr)
	{
		Ports->SetSecondRead(Screen->ReadsSecondPad());
		Pads.bMotor = Ports->FirstHasMotor();
		Pads.bSecondConnected = Ports->HasSecond();
		Pads.SecondHeld = Ports->SecondHeld();
	}
	Screen->SetPads(Pads);
	Weltall::FPadState Held = Controller ? Controller->HeldPad() : Weltall::FPadState{};
	if (UKeyListenerSubsystem* Keys = GetGameInstance()->GetSubsystem<UKeyListenerSubsystem>())
	{
		Held = Keys->Filter(Held);
	}
	Screen->Update(DeltaTime, Held);
	if (Ports != nullptr)
	{
		const std::array<bool, 2> Motors = Screen->Motors();
		Ports->SetMotors(Motors[0], Motors[1]);
	}
}

ETickableTickType UGameScreenSubsystem::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UGameScreenSubsystem::IsTickable() const
{
	return Screen.IsValid();
}

UWorld* UGameScreenSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance()->GetWorld();
}

TStatId UGameScreenSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGameScreenSubsystem, STATGROUP_Tickables);
}
