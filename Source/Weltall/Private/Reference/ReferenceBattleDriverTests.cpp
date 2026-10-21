#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleContext.h"
#include "Misc/AutomationTest.h"
#include "Reference/ReferenceBattleDriver.h"

namespace
{
	Weltall::FBattleContext CommandContext()
	{
		Weltall::FBattleContext Context;
		Context.Party.InputMode = 1;
		Context.Party.Depth = 1;
		Context.Party.Routines[0].Routine = Weltall::EBattlePartyRoutine::Turn;
		Context.Party.MenuState = 1;
		Context.Party.bIconsShown = true;
		return Context;
	}

	uint16 Choose(const Weltall::FBattleContext* Context, bool bEnabled = true)
	{
		WeltallReference::FReferenceBattleDriver Driver;
		if (bEnabled) Driver.Parse(TEXT("-BattleDriver=StoreCombo"));
		return Driver.Step(Context, 0.25).Buttons;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceBattleSafety, "Weltall.Reference.BattleDriver.InputBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FReferenceBattleSafety::RunTest(const FString&)
{
	auto Context = CommandContext();
	TestEqual(TEXT("Absent option leaves normal input alone"), Choose(&Context, false), uint16(0));
	TestEqual(TEXT("No battle emits no buttons"), Choose(nullptr), uint16(0));
	Context.End = 1;
	TestEqual(TEXT("Results remain available for review"), Choose(&Context), uint16(0));
	Context.End = 0;
	Context.ApRecords[0].InGear = 1;
	TestEqual(TEXT("On-foot strategy does not control gears"), Choose(&Context), uint16(0));
	Context.ApRecords[0].InGear = 0;
	Context.Party.InputMode = 2;
	TestEqual(TEXT("Battle dialogue belongs to the separate reader"), Choose(&Context), uint16(0));
	Context.Party.InputMode = 1;
	Context.Party.Depth = 0;
	TestEqual(TEXT("Playback without a command routine is untouched"), Choose(&Context), uint16(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceBattleInput, "Weltall.Reference.BattleDriver.StoreAndRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FReferenceBattleInput::RunTest(const FString&)
{
	using namespace Weltall;
	auto Context = CommandContext();
	TestEqual(TEXT("Open the ordinary Attack command"), Choose(&Context), FPadState::Bit(EPadButton::Cross));
	Context.Party.MenuState = 5;
	TestEqual(TEXT("Make a weak attack before saving AP"), Choose(&Context), FPadState::Bit(EPadButton::Triangle));
	Context.Party.bStoresAp = true;
	Context.Party.MenuState = 100;
	TestEqual(TEXT("End through ordinary Cancel after a hit"), Choose(&Context), FPadState::Bit(EPadButton::Circle));
	WeltallReference::FReferenceBattleDriver Driver;
	Driver.Parse(TEXT("-BattleDriver=StoreCombo"));
	TestEqual(TEXT("Press starts after the release interval"), Driver.Step(&Context, 0.25).Buttons, FPadState::Bit(EPadButton::Circle));
	TestEqual(TEXT("A long update releases the held button"), Driver.Step(&Context, 0.1).Buttons, uint16(0));
	TestEqual(TEXT("No next-frame repeat"), Driver.Step(&Context, 1.0 / 60).Buttons, uint16(0));
	Driver.Step(nullptr, 1.0 / 60);
	TestEqual(TEXT("Returning to a battle starts with released input"), Driver.Step(&Context, 1.0 / 60).Buttons, uint16(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReferenceBattleCombo, "Weltall.Reference.BattleDriver.ComboMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FReferenceBattleCombo::RunTest(const FString&)
{
	using namespace Weltall;
	auto Context = CommandContext();
	Context.ApRecords[0].StoredAp = 28;
	TestEqual(TEXT("Full AP selects Combo normally"), Choose(&Context), FPadState::Bit(EPadButton::Right));
	Context.Party.MenuState = 7;
	TestEqual(TEXT("Open Combo"), Choose(&Context), FPadState::Bit(EPadButton::Cross));
	Context.Party.Routines[0].Routine = EBattlePartyRoutine::ComboMenu;
	Context.Party.ListWindow = 3;
	Context.Party.Combo.Offered[0] = 0;
	Context.Party.Combo.Costs[0] = 4;
	Context.Party.Combo.Pool = 4;
	TestEqual(TEXT("Queue only an affordable learned move"), Choose(&Context), FPadState::Bit(EPadButton::Cross));
	Context.Party.Combo.Pool = 0;
	TestEqual(TEXT("An empty queue is not accepted"), Choose(&Context), uint16(0));
	Context.Party.Combo.Queued = 1;
	TestEqual(TEXT("Move to Accept when the pool is spent"), Choose(&Context), FPadState::Bit(EPadButton::Triangle));
	Context.Party.Combo.Cursor = 7;
	TestEqual(TEXT("Confirm the visible Accept cell"), Choose(&Context), FPadState::Bit(EPadButton::Cross));
	Context.Party.ListWindow = 0;
	Context.Party.Depth = 2;
	Context.Party.Routines[1].Routine = EBattlePartyRoutine::SelectTarget;
	TestEqual(TEXT("Confirm Combo's ordinary target"), Choose(&Context), FPadState::Bit(EPadButton::Cross));
	Context.Party.Routines[0].Routine = EBattlePartyRoutine::ItemMenu;
	TestEqual(TEXT("Unrelated target menus are untouched"), Choose(&Context), uint16(0));
	return true;
}

#endif
