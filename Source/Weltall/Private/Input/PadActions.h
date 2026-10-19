#pragma once

#include "CoreMinimal.h"
#include "Field/FieldPlayerCamera.h"
#include "Input/ControlBindings.h"
#include "Input/PadState.h"
#include "InputActionValue.h"
#include "UObject/Object.h"

#include "PadActions.generated.h"

class UEnhancedPlayerInput;
class UInputAction;
class UInputMappingContext;

/**
 * The player's controls as Enhanced Input sees them: an action for each of the PlayStation
 * controller's buttons and for the field's view, mapped to the keys and gamepad buttons the player's
 * bindings name.
 */
UCLASS()
class UPadActions : public UObject
{
	GENERATED_BODY()

public:
	/** Creates the actions, owned by Outer, with no keys mapped yet. */
	static UPadActions* Create(UObject* Outer);

	const UInputMappingContext* Mapping() const { return Keys; }

	/** Maps the actions to the bindings' keys in place of the ones they had. */
	void Map(const Weltall::FControlBindings& Bindings);

	/**
	 * The buttons held as the player's input last evaluated its actions, with the directions only a stick holds and
	 * that stick's tilt (PadStick).
	 */
	Weltall::FPadState ReadHeld(const UEnhancedPlayerInput& Input) const;

	/**
	 * What the player did to the field's view over DeltaSeconds: a stick or key pushed all the way
	 * moves it at FFieldViewInput's rates, the mouse by how far it moved, each scaled by the player's
	 * sensitivity; the reset once as it is pressed.
	 */
	Weltall::FFieldViewInput ReadView(const UEnhancedPlayerInput& Input, float DeltaSeconds);

private:
	UInputAction* MakeAction(const TCHAR* Name, EInputActionValueType Type);

	/** One action for each of Weltall::EControlAction. */
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;

	/** The view's axes again for the mouse, whose axes report how far it moved rather than how far it is pushed. */
	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Movements;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> Keys;

	/** The bindings last mapped, and the stick their directions are on (PadStick::StickOf). */
	Weltall::FControlBindings Bound;
	int32 Stick = 0;

	bool bResetHeld = false;
	/** The bindings' sensitivities as the view's scales: degrees for a count of the mouse, and the stick's rates' factor. */
	float MouseDegrees = Weltall::MouseDegreesPerCount(Weltall::DefaultSensitivity);
	float StickScale = 1.0f;
};
