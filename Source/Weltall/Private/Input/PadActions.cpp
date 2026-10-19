#include "Input/PadActions.h"

#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Input/PadStick.h"
#include "WeltallLog.h"

namespace
{
	using Weltall::EControlAction;
	using Weltall::IsViewAxis;

	constexpr const TCHAR* ActionNames[Weltall::ControlActionCount] = {TEXT("Up"), TEXT("Down"), TEXT("Left"), TEXT("Right"), TEXT("Cross"),
		TEXT("Circle"), TEXT("Square"), TEXT("Triangle"), TEXT("L1"), TEXT("R1"), TEXT("L2"), TEXT("R2"), TEXT("Start"), TEXT("Select"),
		TEXT("ViewTurn"), TEXT("ViewTilt"), TEXT("ViewZoom"), TEXT("ViewReset")};

	/** A stick's travel the player's resting thumb does not reach. */
	constexpr float StickDeadZone = 0.2f;
	/** How far a wheel's notch zooms the view (halvings of the distance). */
	constexpr float ZoomPerWheelNotch = 0.125f;

	/** The mouse's axes report how far it moved in the frame, not how far it is pushed. */
	bool IsMovement(const FKey& Key)
	{
		return Key == EKeys::MouseX || Key == EKeys::MouseY || Key == EKeys::MouseWheelAxis;
	}
}

UPadActions* UPadActions::Create(UObject* Outer)
{
	UPadActions* Pad = NewObject<UPadActions>(Outer, TEXT("PadActions"));
	Pad->Actions.SetNum(Weltall::ControlActionCount);
	Pad->Movements.SetNum(Weltall::ControlActionCount);
	for (int32 Index = 0; Index < Weltall::ControlActionCount; ++Index)
	{
		const EControlAction Action = static_cast<EControlAction>(Index);
		const EInputActionValueType Type = IsViewAxis(Action) ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
		Pad->Actions[Index] = Pad->MakeAction(ActionNames[Index], Type);
		if (IsViewAxis(Action))
		{
			Pad->Movements[Index] = Pad->MakeAction(*FString::Printf(TEXT("%sByMouse"), ActionNames[Index]), Type);
		}
	}
	Pad->Keys = NewObject<UInputMappingContext>(Pad, TEXT("PlayerKeys"));
	return Pad;
}

UInputAction* UPadActions::MakeAction(const TCHAR* Name, EInputActionValueType Type)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = Type;
	return Action;
}

void UPadActions::Map(const Weltall::FControlBindings& Bindings)
{
	Keys->UnmapAll();
	Bound = Bindings;
	Stick = PadStick::BoundStick(Bindings);
	MouseDegrees = Weltall::MouseDegreesPerCount(Bindings.MouseSensitivity);
	StickScale = Weltall::StickRateScale(Bindings.StickSensitivity);
	for (int32 Index = 0; Index < Weltall::ControlActionCount; ++Index)
	{
		const EControlAction Action = static_cast<EControlAction>(Index);
		for (const std::string& Binding : Bindings.Keys(Action))
		{
			const std::string_view Name = Weltall::BoundKeyName(Binding);
			const FKey Key(FName(static_cast<int32>(Name.size()), Name.data()));
			if (!Key.IsValid())
			{
				UE_LOG(LogWeltall, Warning, TEXT("The controls name a key there is none of: %hs"), Binding.c_str());
				continue;
			}
			UInputAction* Target = IsViewAxis(Action) && IsMovement(Key) ? Movements[Index].Get() : Actions[Index].Get();
			FEnhancedActionKeyMapping& Mapping = Keys->MapKey(Target, Key);
			if (Key.IsGamepadKey() && Key.IsAxis1D())
			{
				UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Keys);
				DeadZone->LowerThreshold = StickDeadZone;
				DeadZone->Type = EDeadZoneType::Axial;
				Mapping.Modifiers.Add(DeadZone);
			}
			if (Weltall::IsNegatedBinding(Binding))
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Keys));
			}
		}
	}
}

Weltall::FFieldViewInput UPadActions::ReadView(const UEnhancedPlayerInput& Input, float DeltaSeconds)
{
	const auto Pushed = [this, &Input](EControlAction Action) { return Input.GetActionValue(Actions[static_cast<int32>(Action)]).Get<float>(); };
	const auto Moved = [this, &Input](EControlAction Action) { return Input.GetActionValue(Movements[static_cast<int32>(Action)]).Get<float>(); };
	Weltall::FFieldViewInput View;
	View.Turn = Pushed(EControlAction::ViewTurn) * Weltall::FFieldViewInput::TurnPerSecond * StickScale * DeltaSeconds + Moved(EControlAction::ViewTurn) * MouseDegrees;
	View.Tilt = Pushed(EControlAction::ViewTilt) * Weltall::FFieldViewInput::TiltPerSecond * StickScale * DeltaSeconds + Moved(EControlAction::ViewTilt) * MouseDegrees;
	View.Zoom = Pushed(EControlAction::ViewZoom) * Weltall::FFieldViewInput::ZoomPerSecond * DeltaSeconds + Moved(EControlAction::ViewZoom) * ZoomPerWheelNotch;
	const bool bReset = Input.GetActionValue(Actions[static_cast<int32>(EControlAction::ViewReset)]).Get<bool>();
	View.bReset = bReset && !bResetHeld;
	bResetHeld = bReset;
	return View;
}

Weltall::FPadState UPadActions::ReadHeld(const UEnhancedPlayerInput& Input) const
{
	Weltall::FPadState Held;
	for (int32 Index = 0; Index < Weltall::ControlActionCount; ++Index)
	{
		const std::optional<Weltall::EPadButton> Button = Weltall::PadButtonOf(static_cast<EControlAction>(Index));
		if (Button && Input.GetActionValue(Actions[Index]).Get<bool>())
		{
			Held.Buttons |= Weltall::FPadState::Bit(*Button);
		}
	}
	Held.StickButtons = PadStick::OnlyStickHolds(Bound, Held, [&Input](const FKey& Key) { return Input.IsPressed(Key); });
	if (Stick != 0)
	{
		const TPair<FKey, FKey> Axes = PadStick::Axes(Stick);
		PadStick::Report(Input.GetRawKeyValue(Axes.Key), Input.GetRawKeyValue(Axes.Value), Held);
	}
	return Held;
}
