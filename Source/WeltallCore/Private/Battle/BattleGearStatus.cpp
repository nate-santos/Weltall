#include "Battle/BattleGameState.h"
#include "Battle/BattleRules.h"
#include "Battle/BattleSteps.h"
#include "Battle/BattleTurn.h"
#include "Foundation/ProcessorDivide.h"

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint8 Billy = 4;
		constexpr uint8 Heimdal = 3;
		constexpr uint8 Xenogears = 15;
		/** The story flag after which Infinity can start. */
		constexpr uint16 InfinityOpen = 0x4000;

		struct FGearStatusMessage
		{
			uint8 Kind;
			uint16 Bits;
			uint8 Message;
		};

		constexpr std::array<FGearStatusMessage, 25> GearStatusMessages{{
			{0, 0x400, 0x24}, {0, 0x200, 0x25}, {0, 0x100, 0x26}, {0, 0x080, 0x27}, {0, 0x040, 0x28}, {0, 0x020, 0x29},
			{0, 0x010, 0x2A}, {0, 0x004, 0x2B},
			{1, 0x1000, 0x2D}, {1, 0x800, 0x2E}, {1, 0x400, 0x2F}, {1, 0x040, 0x15}, {1, 0x020, 0x16}, {1, 0x008, 0x19},
			{1, 0x002, 0x19}, {1, 0x004, 0x1A}, {1, 0x001, 0x1A},
			{3, 0x8000, 0x1B}, {3, 0x4000, 0x1C}, {3, 0x2000, 0x1D}, {3, 0x1000, 0x1E}, {3, 0x400, 0x1F}, {3, 0x800, 0x20},
			{3, 0x100, 0x21}, {3, 0x200, 0x22},
		}};

		void SetGearStatusMessage(FBattleContext& Context, uint8 Kind, uint16 Bits)
		{
			for (const FGearStatusMessage& Entry : GearStatusMessages)
			{
				if (Entry.Kind == Kind && Entry.Bits == Bits)
				{
					Context.Action.Message = Entry.Message;
					return;
				}
			}
		}

		/** A gear status's turns, kept among its pilot's durations. */
		void SetGearDuration(FBattleCombatant& Holder, uint16 Bits, uint8 Turns)
		{
			switch (Bits)
			{
			case 0x100: Holder.Durations[2] = Turns; break;
			case 0x020:
				Holder.Durations[4] = Turns;
				Holder.Status |= 0x1000;
				break;
			case 0x010: Holder.Durations[5] = Turns; break;
			case 0x080: Holder.Durations[3] = Turns; break;
			case 0x200: Holder.Durations[1] = Turns; break;
			default: break;
			}
		}
	}

	namespace BattleSteps
	{
		bool GearApplyStatus(FBattleContext& Context, bool bWeapon)
		{
			FBattleAction& Action = Context.Action;
			int32 Chance = 0;
			int32 Kind = 0;
			uint16 Bits = 0;
			uint8 Turns = 5;
			if (!bWeapon)
			{
				const FBattleCommand& Command = BattleRules::Command(Context);
				Chance = static_cast<int8>(Command.StatusChance);
				Kind = static_cast<int8>(Command.StatusGroup);
				Bits = Command.StatusBits;
				Turns = Command.Power;
			}
			else
			{
				const FBattleGearWeaponSlot& Weapon = ActorGear(Context).Weapons[0];
				Chance = Weapon.StatusChance;
				Kind = static_cast<int8>(Weapon.StatusKind);
				Bits = Weapon.StatusBits;
			}
			const uint8 TargetSlot = Action.TargetSlot;
			if (!InGear(Context, TargetSlot) || Chance < Percent(Context))
			{
				return false;
			}
			FBattleGearPart& Gear = TargetGear(Context);
			FBattleCombatant& Target = BattleRules::Target(Context);
			FBattleCombatant& Holder = Context.Combatants[TargetSlot];
			if (Kind == 0 && (Bits & 0x20) != 0 && (Gear.Buffs & 0x8000) != 0)
			{
				Gear.Buffs &= 0x7FFF;
				Target.Buffs &= 0x7FFF;
				return true;
			}
			if (Kind == 1)
			{
				if ((Bits & 0xA) != 0)
				{
					if ((Gear.Buffs & 5) != 0)
					{
						Gear.Buffs &= 0xFFFA;
						return true;
					}
				}
				else if ((Bits & 5) != 0 && (Gear.Buffs & 0xA) != 0)
				{
					Gear.Buffs &= 0xFFF5;
					return true;
				}
			}
			if (Kind == 0)
			{
				if ((Bits & Gear.StatusImmunity) != 0)
				{
					return false;
				}
				if (Bits == 0x400)
				{
					Holder.Durations[0] = Turns;
					Gear.Status &= 0xFBFF;
					Holder.Status |= 0x2000;
				}
				else if (Bits == 0x1000)
				{
					Gear.Status &= 0xEFFF;
					Holder.Mental |= 0x2000;
				}
				else
				{
					SetGearDuration(Holder, Bits, Turns);
				}
				Gear.Status |= Bits;
			}
			if (Kind == 1)
			{
				Gear.Buffs |= Bits;
				switch (Bits)
				{
				case 0x40: Holder.Durations[11] = Turns; break;
				case 0x20: Holder.Durations[12] = Turns; break;
				case 0x1000: Holder.Durations[10] = Turns; break;
				default: break;
				}
			}
			if (Kind == 3)
			{
				if ((Bits & 0xF000) != 0)
				{
					if ((Gear.PermanentElements & 0xF000) != 0)
					{
						return false;
					}
					Gear.Elements = static_cast<uint16>(Bits | (Gear.Elements & 0x0FFF));
					Holder.Durations[7] = Turns;
				}
				if ((Bits & 0x0F00) != 0)
				{
					if ((Gear.PermanentElements & 0x0F00) != 0)
					{
						return false;
					}
					Gear.Elements = static_cast<uint16>(Bits | (Gear.Elements & 0xF0FF));
					Holder.Durations[8] = Turns;
				}
			}
			if (Kind == 10)
			{
				Gear.Elements &= static_cast<uint16>(~Bits);
			}
			if (Kind == 11 && (Gear.StatusImmunity & 0x40) == 0)
			{
				Gear.ArmorBreak = static_cast<uint8>(Gear.ArmorBreak + Bits);
				Kind = 0;
				if (Gear.ArmorBreak > 99)
				{
					Gear.ArmorBreak = 99;
				}
				Bits = 0x40;
			}
			if (Kind == 12)
			{
				Action.Results[TargetSlot] = BattleResults::HpDamage;
				Action.Amounts[TargetSlot] = ProcessorDivideUnsigned(Gear.Hp, Bits);
			}
			if (Kind == 13)
			{
				Action.Results[TargetSlot] = BattleResults::HpDamage;
				Action.Amounts[TargetSlot] = Gear.Hp - 1;
			}
			if (Kind == 14)
			{
				Gear.Buffs = 0;
				Gear.Elements = 0;
				if (Bits == 1)
				{
					Gear.PermanentBuffs = 0;
					Gear.PermanentElements = 0;
				}
			}
			if (Kind == 16)
			{
				Gear.Status |= 1;
				Target.Status |= 0x80;
			}
			if (Kind == 15)
			{
				Gear.Status &= 0xFFFE;
				Target.Status &= 0xFF7F;
			}
			SetGearStatusMessage(Context, static_cast<uint8>(Kind), Bits);
			return true;
		}
	}

	void PrepareGearTurn(FBattleContext& Context, uint8 Slot)
	{
		FBattleGearTurn& Turn = Context.GearTurn;
		FBattleCombatant& Member = Context.Combatants[Slot];
		FBattleGearPart& Gear = Member.GearPart;
		for (int32 Command = 0; Command < static_cast<int32>(Turn.Costs.size()); ++Command)
		{
			Turn.Costs[static_cast<std::size_t>(Command)] = Context.Commands[FBattleContext::GearCommandIndex(Slot, Command)].FuelCost;
		}
		// Perfect Works' rebalance charges by 10 where the original takes the weak attack's fuel cost.
		const uint16 ChargeBase = Context.bPerfectWorksRebalance ? uint16{10} : Turn.Costs[0];
		Turn.Charge = Gear.ChargeMultiplier == 0 ? 30 : static_cast<uint16>(Gear.ChargeMultiplier * ChargeBase);
		const uint8 Level = Member.AttackLevel;
		if (Level >= 1 && Level <= 3)
		{
			Turn.Charge = static_cast<uint16>(Turn.Charge + Level * 20);
		}
		else if (Level == 4)
		{
			Turn.Charge = static_cast<uint16>(Turn.Charge * 10);
		}
		Turn.ShownAttack = static_cast<uint16>(Gear.Weapons[0].Attack + Gear.Output * Gear.AttackMultiplier);
		if (Member.Character == Billy)
		{
			Turn.ShownAttack = static_cast<uint16>(Gear.Weapons[2].Attack + Turn.ShownAttack);
		}
		Turn.Agility = Gear.Agility;
		Turn.ArmorBreak = Gear.ArmorBreak;
		// At full HP the game reads a register its caller left holding the slot; the fix counts a tenth lost.
		uint32 Lost = Context.RuleOptions.bInfinityByDamageAlone ? 1u : Slot + 1u;
		if (Gear.MaxHp != Gear.Hp)
		{
			Lost = ProcessorDivideUnsigned(Gear.MaxHp - Gear.Hp, Gear.MaxHp / 10);
			if ((Lost & 0xFF) == 0)
			{
				++Lost;
			}
		}
		uint32 Chance = (Lost & 0xFF) * (Member.InfinityCounter + 5u);
		const bool bOpen = (BattleGameState::Word(*Context.GameState, GameStateLayout::StoryFlagsOffset) & InfinityOpen) != 0;
		if (!bOpen || Member.Level < 50 || Member.Gear == Heimdal)
		{
			Chance = 0;
		}
		uint32 Shown = Chance & 0xFF;
		if (Member.Gear == Xenogears)
		{
			Chance = 99;
			Shown = 99;
		}
		Turn.InfinityChance = static_cast<uint8>(Chance);
		if (Shown > 99)
		{
			Chance = 99;
			Turn.InfinityChance = 99;
		}
		Turn.Lamps = 0;
		if ((Gear.Status & 0x100) != 0) Turn.Lamps = 0x8000;
		if ((Gear.Status & 0x200) != 0) Turn.Lamps |= 0x4000;
		if ((Gear.Status & 0x080) != 0) Turn.Lamps |= 0x2000;
		if ((Gear.Status & 0x010) != 0) Turn.Lamps |= 0x1000;
		if (Gear.Fuel < (Gear.MaxFuel >> 3)) Turn.Lamps |= 0x800;
		if (Gear.Fuel == 0 && Context.bPerfectWorksRebalance)
		{
			// Its booster lives in the gear's permanent buffs.
			Gear.PermanentBuffs &= 0x7FFF;
		}
		else if (Gear.Fuel == 0)
		{
			Gear.Buffs &= 0x7FFF;
			Member.Buffs &= 0x7FFF;
		}
		Turn.bBooster = (Gear.Buffs & 0x8000) != 0;
		if (Level == 4)
		{
			Turn.Level = 4;
			Member.Durations[6] = static_cast<uint8>(Member.Durations[6] - 1);
			if (Member.Durations[6] == 0)
			{
				Gear.Buffs &= 0xBFFF;
				Member.AttackLevel = 0;
				Member.InfinityCounter = 0;
			}
			return;
		}
		Turn.Level = Level;
		if (Level == 3 && bOpen && Percent(Context) < static_cast<int32>(Chance & 0xFF))
		{
			Gear.Buffs |= 0x4000;
			Member.Durations[6] = (Member.EquipmentFlags & 0x40) != 0 ? 6 : 3;
			Member.AttackLevel = static_cast<uint8>(Member.AttackLevel + 1);
		}
	}
}
