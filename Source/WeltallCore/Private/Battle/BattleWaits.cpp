#include "Battle/BattleWaits.h"

#include "Battle/BattleRules.h"

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint16 KnockedOutOrOut = 0xC000;
		constexpr uint16 PlayDead = 0x0002;

		/**
		 * The speed a party slot's next wait takes from the command number the last action left. A
		 * number past the slot's own table reads the tables after it; the record after a gear table's
		 * last holds, in the game, whatever its heap left there when the battle began (a byte of the
		 * screen before the battle, or older data), so it reads as an empty record, whose speed is 0.
		 */
		int32 PartyCommandSpeed(FBattleContext& Context, uint16 Index)
		{
			const bool bPastGearTable = Index >= FBattleContext::FirstGearCommand && Index < FBattleContext::FirstEnemyCommand
				&& !FBattleContext::IsLoadedCommand(Index);
			return bPastGearTable ? 0 : CommandAt(Context, Index).Speed;
		}
	}

	uint8 RollBattleWait(FBattleContext& Context, int32 Slot)
	{
		PointActorAt(Context, Slot);
		const FBattleCombatant& Combatant = Context.Combatants[static_cast<std::size_t>(Slot)];
		uint32 Pace = 0;
		if (Context.IsParty(Slot))
		{
			const bool bGear = Combatant.IsInGear();
			Context.Action.CommandIndex = bGear ? FBattleContext::GearCommandIndex(Slot, Context.Action.Command)
												: FBattleContext::PartyCommandIndex(Slot, Context.Action.Command);
			const int32 Speed = PartyCommandSpeed(Context, Context.Action.CommandIndex);
			const int32 Agility = bGear ? Combatant.GearPart.Agility : Combatant.Agility;
			Pace = Speed < Agility ? 9 * static_cast<uint32>(Agility - Speed) : 9;
		}
		else
		{
			Pace = 9 * static_cast<uint32>(Combatant.Agility);
		}
		if ((Pace & 0xFFFF) > 0xA5)
		{
			Pace = 0xA0;
		}
		const int32 Roll = Draw(Context);
		EndPlayDeadIfAlone(Context);
		return static_cast<uint8>(0xA5 - Pace - static_cast<uint32>(Roll % 8 - 4));
	}

	void InitBattleWaits(FBattleContext& Context)
	{
		for (int32 Slot = 0; Slot < FBattleContext::SlotCount; ++Slot)
		{
			const std::size_t Index = static_cast<std::size_t>(Slot);
			if (!Context.InBattle[Index])
			{
				Context.Waits[Index] = 0xFF;
				Context.FullWaits[Index] = 0xFF;
			}
			else
			{
				const uint8 Wait = RollBattleWait(Context, Slot);
				Context.Waits[Index] = Wait;
				Context.FullWaits[Index] = Wait;
			}
			Context.Ready[Index] = 0;
			Context.SlowToggles[Index] = 0;
		}
	}

	void TickBattleWaits(FBattleContext& Context)
	{
		if (!Context.bWaitsRunning)
		{
			return;
		}
		for (std::size_t Slot = 0; Slot < FBattleContext::SlotCount; ++Slot)
		{
			if (!Context.InBattle[Slot] || Context.Ready[Slot] != 0)
			{
				continue;
			}
			FBattleCombatant& Combatant = Context.Combatants[Slot];
			uint16 Speeds = Combatant.Buffs | Combatant.PermanentBuffs;
			// Perfect Works' rebalance keeps a gear's booster in the gear's permanent buffs, which speed its pilot while boarded.
			if (Context.bPerfectWorksRebalance && Combatant.IsInGear())
			{
				Speeds |= Combatant.GearPart.PermanentBuffs;
			}
			const uint32 Step = (Speeds & 0x8000) != 0 ? 2 : 1;
			if ((Combatant.Status & 0x1000) != 0)
			{
				Context.SlowToggles[Slot] ^= 1;
				if (Context.SlowToggles[Slot] != 0)
				{
					continue;
				}
			}
			if ((Combatant.Status & 0x2000) != 0)
			{
				uint8& Stop = Combatant.Durations[0];
				Stop = static_cast<uint8>(Stop - Step);
				if (Stop == 0)
				{
					Combatant.Status &= 0xDFFF;
					if (Context.RuleOptions.bGearStopEnds)
					{
						// The gear's mark of the Stop, which makes every hit on it land.
						Combatant.GearPart.Status &= 0xFBFF;
					}
				}
			}
			else if ((Combatant.Status & 0x80) == 0 && (Combatant.Mental & 0x1000) == 0)
			{
				Context.Waits[Slot] = static_cast<uint16>(Context.Waits[Slot] - Step);
				if (static_cast<int16>(Context.Waits[Slot]) < 1)
				{
					Context.Ready[Slot] = 1;
					Context.Waits[Slot] = 0;
				}
			}
		}
	}

	void PickBattleActor(FBattleContext& Context)
	{
		if (Context.ForcedActor != 0)
		{
			if (Context.ForcedActor > FBattleContext::SlotCount)
			{
				Context.Fault = EBattleFault::SlotOutsideBattle;
				return;
			}
			const std::size_t Slot = static_cast<std::size_t>(Context.ForcedActor - 1);
			Context.TurnActor = Context.ForcedActor;
			Context.Ready[Slot] = 1;
			Context.ForcedActor = 0;
			Context.Waits[Slot] = 0;
			return;
		}
		Context.TurnActor = 0;
		std::size_t Index = Context.TurnOrderNext;
		for (;;)
		{
			const uint8 Slot = Context.TurnOrder[Index];
			if (Context.Ready[Slot] == 1)
			{
				Context.TurnActor = static_cast<uint8>(Slot + 1);
				Context.TurnOrderNext = static_cast<uint8>(Index + 1 == FBattleContext::SlotCount ? 0 : Index + 1);
				return;
			}
			Index = Index + 1 == FBattleContext::SlotCount ? 0 : Index + 1;
			if (Index == Context.TurnOrderNext)
			{
				return;
			}
		}
	}

	void RollActorWait(FBattleContext& Context)
	{
		const std::size_t Slot = Context.TurnActor;
		if (Context.Ready[Slot] != 0xFF)
		{
			Context.Ready[Slot] = 0;
		}
		const uint8 Wait = RollBattleWait(Context, static_cast<int32>(Slot));
		Context.Waits[Slot] = Wait;
		Context.FullWaits[Slot] = Wait;
	}

	void ResetRevivedWaits(FBattleContext& Context)
	{
		if ((Context.Action.Revived & 7) != 0)
		{
			for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
			{
				if ((Context.Action.Revived & SlotBit(static_cast<int32>(Slot))) != 0)
				{
					Context.Ready[Slot] = 0;
					Context.Waits[Slot] = Context.FullWaits[Slot];
					Context.PanelRefresh[Slot] = 1;
				}
			}
		}
		Context.Action.Revived = 0;
	}

	void EndPlayDeadIfAlone(FBattleContext& Context)
	{
		int32 Out = 0;
		int32 PlayingDead = 0;
		for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
		{
			Out += (Context.Combatants[Slot].Status & KnockedOutOrOut) != 0 ? 1 : 0;
			PlayingDead += (Context.Combatants[Slot].Status & PlayDead) != 0 ? 1 : 0;
		}
		if ((Out == 1 && PlayingDead == 2) || (Out == 2 && PlayingDead == 1))
		{
			for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
			{
				FBattleCombatant& Member = Context.Combatants[Slot];
				if ((Member.Status & PlayDead) != 0)
				{
					Member.Status &= static_cast<uint16>(~PlayDead);
					Member.DisabledCommands = Context.StartingDisabledCommands[Slot];
				}
			}
		}
	}
}
