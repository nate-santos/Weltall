#include "Input/WeltallPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Input/PadActions.h"
#include "Input/KeyListenerSubsystem.h"
#include "Input/PadPortsSubsystem.h"
#include "Input/TouchControls/SWeltallTouchControls.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CoreDelegates.h"
#include "Shell/PlayerSettingsSubsystem.h"
#include "Shell/GameWindowSubsystem.h"
#include "Shell/AndroidDiagnostics.h"
#include "Shell/GameVersion.h"
#include "Widgets/Layout/SSafeZone.h"

Weltall::FPadState AWeltallPlayerController::HeldPad() const
{
	const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (IsTouchMenuOpen())
	{
		return {};
	}
	const Weltall::FPadState Hardware = Input != nullptr && Pad != nullptr ? Pad->ReadHeld(*Input) : Weltall::FPadState{};
	const Weltall::FPadState Touch = TouchControls ? TouchControls->HeldPad() : Weltall::FPadState{};
	return Weltall::MergePads(Weltall::MergePads(Hardware, Touch), AndroidBack.TakeForTick());
}

bool AWeltallPlayerController::HasTouchControls() const
{
	return TouchControls.IsValid();
}

bool AWeltallPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
#if PLATFORM_ANDROID
	const bool bResumeButton = Params.Key.IsGamepadKey() || Params.Key == EKeys::Android_Back
		|| Params.Key == EKeys::Escape || Params.Key == EKeys::Enter || Params.Key == EKeys::SpaceBar;
	if (Params.Event == IE_Pressed && bResumeButton)
	{
		if (UGameWindowSubsystem* Window = GetGameInstance()->GetSubsystem<UGameWindowSubsystem>(); Window && Window->ResumeAudioIfNeeded())
		{
			ResetAllInput();
			return true;
		}
	}
	if (Params.Key == EKeys::Android_Back)
	{
		if (Params.Event == IE_Pressed)
		{
			AndroidBack = {};
			if (TouchControls && TouchControls->HandleBack())
			{
				return true;
			}
			UKeyListenerSubsystem* Keys = GetGameInstance()->GetSubsystem<UKeyListenerSubsystem>();
			if (Keys != nullptr && Keys->IsListening())
			{
				Keys->Heard(EKeys::Escape, 0);
				return true;
			}
			AndroidBack.Sample({Weltall::FPadState::Bit(Weltall::EPadButton::Circle)});
		}
		else if (Params.Event == IE_Released)
		{
			AndroidBack.Sample({});
		}
		// Back is a logical cancel. Android's default handling must not finish the activity.
		return true;
	}
#endif
	return Super::InputKey(Params);
}

void AWeltallPlayerController::FlushPressedKeys()
{
	Super::FlushPressedKeys();
	if (UEnhancedInputLocalPlayerSubsystem* Input = InputSubsystem())
	{
		// A disconnected pad cannot send releases. Discard cached actions and their
		// ignore-until-release flags so the next controller's first press also works.
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = false;
		Input->RequestRebuildControlMappings(Options, EInputMappingRebuildType::RebuildWithFlush);
	}
}

void AWeltallPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (IsTouchMenuOpen())
	{
		AndroidBack = {};
	}
}

bool AWeltallPlayerController::IsTouchMenuOpen() const
{
	return TouchControls && TouchControls->BlocksGameInput();
}

Weltall::FFieldViewInput AWeltallPlayerController::ReadView(float DeltaSeconds)
{
	const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PlayerInput);
	return Input != nullptr && Pad != nullptr ? Pad->ReadView(*Input, DeltaSeconds) : Weltall::FFieldViewInput{};
}

void AWeltallPlayerController::BeginPlay()
{
	Super::BeginPlay();
	Pad = UPadActions::Create(this);
	UPlayerSettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UPlayerSettingsSubsystem>();
	Pad->Map(Settings->Settings().Controls);
	if (UEnhancedInputLocalPlayerSubsystem* Input = InputSubsystem())
	{
		Input->AddMappingContext(Pad->Mapping(), 0);
	}
	SettingsChanged = Settings->OnSettingsChanged.AddUObject(this, &AWeltallPlayerController::FollowBindings);
	// Desktop keeps its normal input. The phone overlay is composed by Slate after the custom game renderer.
#if PLATFORM_ANDROID
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		TouchOverlay = SNew(SSafeZone).IsTitleSafe(true)
		[
			SAssignNew(TouchControls, SWeltallTouchControls)
			.Options(Settings->Settings().Touch)
			.BuildLabel(GameVersion())
			.OnExportDiagnostics_Lambda([] { return ExportAndroidDiagnostics(); })
			.AudioPaused_Lambda([Weak = TWeakObjectPtr<UGameInstance>(GetGameInstance())]()
			{
				UGameInstance* Game = Weak.Get();
				UGameWindowSubsystem* Window = Game ? Game->GetSubsystem<UGameWindowSubsystem>() : nullptr;
				return Window && Window->IsHeldForAudio();
			})
			.CanResumeAudio_Lambda([Weak = TWeakObjectPtr<UGameInstance>(GetGameInstance())]()
			{
				UGameInstance* Game = Weak.Get();
				UGameWindowSubsystem* Window = Game ? Game->GetSubsystem<UGameWindowSubsystem>() : nullptr;
				return Window && Window->CanResumeAudio();
			})
			.OnResumeAudio_Lambda([Weak = TWeakObjectPtr<UGameInstance>(GetGameInstance())]()
			{
				UGameInstance* Game = Weak.Get();
				UGameWindowSubsystem* Window = Game ? Game->GetSubsystem<UGameWindowSubsystem>() : nullptr;
				return Window && Window->ResumeAudioIfNeeded();
			})
			.OnOptionsChanged_Lambda([Weak = TWeakObjectPtr<UPlayerSettingsSubsystem>(Settings)](const Weltall::FTouchOptions& Options)
			{
				if (UPlayerSettingsSubsystem* Kept = Weak.Get())
				{
					Weltall::FPlayerSettings Changed = Kept->Settings();
					Changed.Touch = Options;
					Kept->Change(Changed);
				}
			})
			.OnCancelBinding_Lambda([Weak = TWeakObjectPtr<UGameInstance>(GetGameInstance())]()
			{
				UGameInstance* Game = Weak.Get();
				UKeyListenerSubsystem* Keys = Game != nullptr ? Game->GetSubsystem<UKeyListenerSubsystem>() : nullptr;
				if (Keys != nullptr && Keys->IsListening())
				{
					// The desktop binding dialog listens before logical pad input; touch users need a way out.
					Keys->Heard(EKeys::Escape, 0);
					return true;
				}
				return false;
			})
		];
		Viewport->AddViewportWidgetContent(TouchOverlay.ToSharedRef(), 20);
	}
	EnteredBackground = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &AWeltallPlayerController::ResetAllInput);
	Deactivated = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &AWeltallPlayerController::ResetAllInput);
	EnteredForeground = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &AWeltallPlayerController::ResetAllInput);
	OrientationChanged = FCoreDelegates::ApplicationReceivedScreenOrientationChangedNotificationDelegate.AddUObject(this, &AWeltallPlayerController::ScreenOrientationChanged);
#endif
	SetInputMode(FInputModeGameOnly());
}

void AWeltallPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetAllInput();
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(EnteredBackground);
	FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(Deactivated);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(EnteredForeground);
	FCoreDelegates::ApplicationReceivedScreenOrientationChangedNotificationDelegate.Remove(OrientationChanged);
	if (TouchOverlay)
	{
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(TouchOverlay.ToSharedRef());
		}
	}
	TouchOverlay.Reset();
	TouchControls.Reset();
	if (UPlayerSettingsSubsystem* Settings = GetGameInstance()->GetSubsystem<UPlayerSettingsSubsystem>())
	{
		Settings->OnSettingsChanged.Remove(SettingsChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void AWeltallPlayerController::UpdateForceFeedback(IInputInterface* InputInterface, const int32 ControllerId)
{
	UPadPortsSubsystem* Ports = GetGameInstance()->GetSubsystem<UPadPortsSubsystem>();
	if (Ports != nullptr && InputInterface != nullptr)
	{
		Ports->SendMotors(*InputInterface);
	}
}

void AWeltallPlayerController::FollowBindings()
{
	const Weltall::FPlayerSettings& Settings = GetGameInstance()->GetSubsystem<UPlayerSettingsSubsystem>()->Settings();
	Pad->Map(Settings.Controls);
	if (TouchControls)
	{
		TouchControls->ApplyOptions(Settings.Touch);
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = InputSubsystem())
	{
		Input->RequestRebuildControlMappings();
	}
}

void AWeltallPlayerController::ResetAllInput()
{
	FlushPressedKeys();
	ResetTouches();
}

void AWeltallPlayerController::ResetTouches()
{
	AndroidBack = {};
	if (TouchControls)
	{
		TouchControls->ResetTouches();
	}
}

void AWeltallPlayerController::ScreenOrientationChanged(int32)
{
	// A 180-degree turn can preserve the viewport size; geometry alone cannot detect it.
	ResetTouches();
}

UEnhancedInputLocalPlayerSubsystem* AWeltallPlayerController::InputSubsystem() const
{
	const ULocalPlayer* Local = GetLocalPlayer();
	return Local != nullptr ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}
