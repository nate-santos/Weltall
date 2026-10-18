#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Field/FieldPlayerCamera.h"
#include "Input/PadState.h"
#include "Input/PadLatch.h"

#include "WeltallPlayerController.generated.h"

class UEnhancedInputLocalPlayerSubsystem;
class UPadActions;
class SWeltallTouchControls;
class SWidget;

/**
 * The player: whatever keyboard, mouse or gamepads they use, plugged in before or during play, the
 * game sees a PlayStation controller, pressed through the keys the player's bindings name.
 */
UCLASS()
class AWeltallPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** The controller buttons the player holds now. */
	Weltall::FPadState HeldPad() const;
	/** The Android controls settings/edit panel holds simulation and audio while open. */
	bool IsTouchMenuOpen() const;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void PlayerTick(float DeltaTime) override;

	/** What the player did to the field's view in the last DeltaSeconds. */
	Weltall::FFieldViewInput ReadView(float DeltaSeconds);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** The gamepads' motors are the game's own, each port's on its controller, in place of the engine's effects. */
	virtual void UpdateForceFeedback(IInputInterface* InputInterface, const int32 ControllerId) override;

private:
	/** The player's settings changed: the keys follow their bindings. */
	void FollowBindings();
	void ResetTouches();
	void ScreenOrientationChanged(int32 Orientation);
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem() const;

	UPROPERTY()
	TObjectPtr<UPadActions> Pad;

	FDelegateHandle SettingsChanged;
	FDelegateHandle EnteredBackground;
	FDelegateHandle Deactivated;
	FDelegateHandle OrientationChanged;
	TSharedPtr<SWeltallTouchControls> TouchControls;
	TSharedPtr<SWidget> TouchOverlay;
	mutable Weltall::FPadLatch AndroidBack;
};
