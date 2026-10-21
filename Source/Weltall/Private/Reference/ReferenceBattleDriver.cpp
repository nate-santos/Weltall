#include "Reference/ReferenceBattleDriver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogReferenceBattleDriver, Log, All);

namespace WeltallReference
{
	bool FReferenceBattleDriver::Parse(const FString& Params)
	{
		FString Mode;
		if (!FParse::Value(*Params, TEXT("BattleDriver="), Mode)) return true;
		if (Mode != TEXT("StoreCombo"))
		{
			UE_LOG(LogReferenceBattleDriver, Error, TEXT("-BattleDriver takes StoreCombo"));
			return false;
		}
		bEnabled = true;
		return true;
	}

	Weltall::FPadState FReferenceBattleDriver::Step(const Weltall::FBattleContext* Context, double Seconds)
	{
		using namespace Weltall;
		if (!bEnabled || !Context || Context->End != BattleEnd::Running || Context->Party.InputMode != 1
			|| Context->Party.Actor >= FBattleContext::PartySlotCount || Context->Party.Depth == 0
			|| Context->Party.Depth > Context->Party.Routines.size()
			|| Context->ApRecords[Context->Party.Actor].InGear != 0)
		{
			Held = {};
			HeldFor = 0.0;
			ReleasedFor = 0.0;
			return {};
		}
		if (Held.Buttons != 0)
		{
			HeldFor += Seconds;
			if (HeldFor + 1e-9 < 0.1) return Held;
			Held = {};
			ReleasedFor = 0.0;
			return {};
		}
		ReleasedFor += Seconds;
		if (ReleasedFor + 1e-9 < 0.25) return {};

		const FBattlePartyInput& Party = Context->Party;
		const FBattlePartyFrame& Frame = Party.Routines[Party.Depth - 1];
		TOptional<EPadButton> Button;
		if (Frame.Routine == EBattlePartyRoutine::Turn && Party.bIconsShown && !Party.bTurnOver)
		{
			// The public party state documents the on-foot command ring: 1 Attack, 7 Combo.
			if (Party.MenuState == 1) Button = Context->ApRecords[Party.Actor].StoredAp >= 28 ? EPadButton::Right : EPadButton::Cross;
			else if (Party.MenuState == 7) Button = EPadButton::Cross;
			// At rest the attack handler has returned to Turn; 100 is its fading command ring.
			else if ((Party.MenuState == 5 || Party.MenuState == 100) && !Party.bComboClosed)
				Button = Party.bStoresAp ? EPadButton::Circle : EPadButton::Triangle;
		}
		else if (Frame.Routine == EBattlePartyRoutine::AttackMode && !Party.bComboClosed)
		{
			// One weak hit permits ending the turn and keeping its remaining AP.
			Button = Party.bStoresAp ? EPadButton::Circle : EPadButton::Triangle;
		}
		else if (Frame.Routine == EBattlePartyRoutine::ComboMenu && Party.ListWindow == 3)
		{
			// Queue the first learned Deathblow while it fits, then select the menu's Accept cell.
			const FBattleComboMenu& Combo = Party.Combo;
			// Seven move cells are followed by the eighth cell, Accept.
			if (Combo.Cursor == Combo.Offered.size()) Button = EPadButton::Cross;
			else if (Combo.Cursor < Combo.Offered.size() && Combo.Offered[Combo.Cursor] != 0xFF
				&& Combo.Costs[Combo.Cursor] != 0 && Combo.Pool >= Combo.Costs[Combo.Cursor]
				&& Combo.Queued < Party.ComboQueue.size()) Button = EPadButton::Cross;
			else if (Combo.Queued != 0) Button = EPadButton::Triangle;
		}
		else if (Frame.Routine == EBattlePartyRoutine::SelectTarget && Party.ListWindow == 0 && Party.Depth >= 2
			&& Party.Routines[Party.Depth - 2].Routine == EBattlePartyRoutine::ComboMenu)
		{
			Button = EPadButton::Cross;
		}
		if (!Button.IsSet()) return {};
		Held.Buttons = FPadState::Bit(Button.GetValue());
		HeldFor = 0.0;
		ReleasedFor = 0.0;
		UE_LOG(LogReferenceBattleDriver, Display, TEXT("BattleDriver pad=%04X actor=%d menu=%d ap=%d stored=%d routine=%d queue=%d pool=%d"),
			Held.Buttons, Party.Actor, Party.MenuState, Party.ApLeft, Context->ApRecords[Party.Actor].StoredAp,
			static_cast<int32>(Frame.Routine), Party.Combo.Queued, Party.Combo.Pool);
		return Held;
	}
}

#endif
