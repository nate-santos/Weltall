#include "Battle/BattleRules.h"
#include "Battle/BattleSteps.h"
#include "Foundation/ProcessorDivide.h"
#include "Game/EtherHealing.h"

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint8 Citan = 2;
		/** The status group that marks a dispel. */
		constexpr uint8 DispelGroup = 0x6E;

		/** The chance of a drain or fixed damage: the actor's status rate, or the command's own. */
		int32 SourcedChance(FBattleContext& Context, const FBattleCommand& Command)
		{
			switch (Command.ChanceSource)
			{
			case 0: return BattleRules::Actor(Context).StatusRate;
			case 1: return Command.StatusChance;
			default: Context.Fault = EBattleFault::UnknownChanceSource; return 0;
			}
		}
	}

	namespace BattleSteps
	{
		void HandlerHeal(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			const FBattleCombatant& Target = BattleRules::Target(Context);
			int16 Heal = EtherHealing(Actor.Ether, Command.Power, Actor.PermanentEtherBuffs, Target.Elements | Target.PermanentElements);
			if (InGear(Context, Action.TargetSlot))
			{
				Heal = 0;
			}
			Action.Results[Action.TargetSlot] = BattleResults::HpHeal;
			Action.Amounts[Action.TargetSlot] = static_cast<uint32>(static_cast<int32>(Heal));
		}

		void HandlerStatus(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand Command = BattleRules::Command(Context);
			const bool bTaken = TryInflictStatus(Context, Command.StatusChance, Command.StatusGroup, Command.StatusBits);
			// The original sets the count before it looks at the outcome.
			const bool bCounts = bTaken || !Context.RuleOptions.bFailedStatusKeepsCount;
			// Perfect Works' rebalance: a Deathblow's status and one on the actor itself last the record's own turns; any
			// other, a hit's too, counts the command's power and marks a miss.
			constexpr uint16 OwnTurns = BattleCommandFlags::Deathblow | BattleCommandFlags::PerfectWorksSelfStatus;
			const bool bRebalance = Context.bPerfectWorksRebalance;
			if ((Command.Flags & (bRebalance ? OwnTurns : BattleCommandFlags::StatusOnHit)) == 0)
			{
				if (bCounts)
				{
					SetStatusDuration(Context, Action.TargetSlot, Command.StatusGroup, Command.StatusBits, Command.Power);
				}
				if (!bTaken)
				{
					Action.Results[Action.TargetSlot] = BattleResults::NoEffect;
				}
			}
			else if (bCounts)
			{
				SetStatusDuration(Context, Action.TargetSlot, Command.StatusGroup, Command.StatusBits, bRebalance ? Command.OwnStatusTurns : uint8{5});
			}
		}

		void HandlerDrain(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			const int32 Chance = SourcedChance(Context, Command) & 0xFF;
			if (Chance < Percent(Context))
			{
				Action.Results[Action.TargetSlot] = BattleResults::NoEffect;
				return;
			}
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			const FBattleCombatant& Target = BattleRules::Target(Context);
			uint32 Base = 0;
			switch (Command.AttackFormula)
			{
			case 0: Base = Actor.MaxHp; break;
			case 1: Base = Target.MaxHp; break;
			case 2: Base = Actor.MaxEp; break;
			case 3: Base = Target.MaxEp; break;
			default: break;
			}
			uint32 Amount = static_cast<uint32>(static_cast<int32>(Base * Command.Power) / 20);
			switch (Command.AttackFormula)
			{
			case 5:
				Amount = static_cast<uint32>(Target.Hp) - 1;
				[[fallthrough]];
			case 0:
			case 1:
				Action.Results[Action.ActorSlot] = BattleResults::HpHeal;
				Action.Results[Action.TargetSlot] = BattleResults::HpDamage;
				break;
			case 2:
			case 3:
				if (((Target.EtherBuffs | Target.PermanentEtherBuffs) & 0x200) != 0)
				{
					Action.Results[Action.TargetSlot] = BattleResults::NoEffect;
					return;
				}
				Action.Results[Action.ActorSlot] = BattleResults::EpHeal;
				Action.Results[Action.TargetSlot] = BattleResults::EpDamage;
				break;
			default:
				return;
			}
			Action.Amounts[Action.ActorSlot] = Amount & 0xFFFF;
			Action.Amounts[Action.TargetSlot] = Amount & 0xFFFF;
		}

		void HandlerFixedDamage(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			const int32 Chance = SourcedChance(Context, Command) & 0xFF;
			if (Chance < Percent(Context))
			{
				Action.Results[Action.TargetSlot] = BattleResults::NoEffect;
				return;
			}
			Action.Results[Action.TargetSlot] = BattleResults::HpDamage;
			FBattleCombatant& Target = BattleRules::Target(Context);
			uint32& Amount = Action.Amounts[Action.TargetSlot];
			const bool bTargetInGear = InGear(Context, Action.TargetSlot);
			switch (Command.AttackFormula)
			{
			case 0:
				Amount = ProcessorDivideUnsigned(bTargetInGear ? TargetGear(Context).Hp : Target.Hp, Command.Power);
				break;
			case 1:
				Amount = (bTargetInGear ? TargetGear(Context).Hp : Target.Hp) - 1;
				break;
			case 2:
				if (InGear(Context, Action.ActorSlot))
				{
					Amount = ActorGear(Context).MaxHp - ActorGear(Context).Hp;
				}
				else
				{
					const FBattleCombatant& Actor = BattleRules::Actor(Context);
					Amount = static_cast<uint32>(Actor.MaxHp) - Actor.Hp;
				}
				break;
			case 3:
				Amount = static_cast<uint32>(Target.Ep) * 10;
				break;
			case 4:
				Amount = 1;
				break;
			case 5:
				Amount = Target.Hp;
				break;
			case 6:
				if (!bTargetInGear)
				{
					Amount = Target.MaxHp;
					if (Target.MaxHp > 9999)
					{
						Target.MaxHp = 9999;
					}
				}
				else
				{
					FBattleGearPart& Gear = TargetGear(Context);
					Amount = Gear.MaxHp;
					if (Gear.MaxHp > 9999)
					{
						Gear.MaxHp = 9999;
					}
				}
				break;
			case 7:
				if (!bTargetInGear)
				{
					Target.Status |= 1;
					Target.Mental |= 1;
				}
				else
				{
					Action.Results[Action.TargetSlot] = BattleResults::NoEffect;
				}
				break;
			default:
				break;
			}
		}

		void HandlerCure(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			if (static_cast<int32>(Command.StatusChance) < Percent(Context))
			{
				Action.Results[Action.TargetSlot] = BattleResults::NoEffect;
				return;
			}
			FBattleCombatant& Target = BattleRules::Target(Context);
			const uint8 Cures = Command.StatusGroup;
			if ((Cures & 0x80) != 0)
			{
				Target.Status &= 0xC000;
			}
			if ((Cures & 0x40) != 0)
			{
				Target.Mental = 0;
				Target.DisabledCommands &= 0xFFDF;
			}
			if ((Cures & 0x20) != 0)
			{
				Target.Buffs = 0;
			}
			if ((Cures & 0x10) != 0)
			{
				Target.EtherBuffs = 0;
			}
			if ((Cures & 0x08) != 0)
			{
				Target.Elements = 0;
			}
		}

		void HandlerRevive(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			if (Context.Combatants[Action.TargetSlot].Character == Citan)
			{
				ResetEtherTargetAll(Context, Action.TargetSlot, true);
			}
			FBattleCombatant& Target = BattleRules::Target(Context);
			Target.Status = 0;
			Target.Mental = 0;
			Target.Buffs = 0;
			Target.EtherBuffs = 0;
			Target.Elements = 0;
			Action.Results[Action.TargetSlot] = BattleResults::HpHeal;
			Action.Amounts[Action.TargetSlot] = static_cast<uint32>(Target.MaxHp) * BattleRules::Command(Context).Power / 10;
			Action.Revived = static_cast<uint16>(Action.Revived | SlotBit(Action.TargetSlot));
		}

		void HandlerRestoreGearHp(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			Action.Results[Action.TargetSlot] = BattleResults::HpHeal;
			Action.Amounts[Action.TargetSlot] = static_cast<uint32>(BattleRules::Command(Context).Power) * TargetGear(Context).MaxHp / 20;
		}

		void ApplyWeaponStatus(FBattleContext& Context)
		{
			const FBattleWeaponSlot Weapon = BattleRules::Actor(Context).Weapons[0];
			if (TryInflictStatus(Context, Weapon.StatusChance, Weapon.StatusGroup, Weapon.StatusBits))
			{
				const FBattleCommand& Command = BattleRules::Command(Context);
				SetStatusDuration(Context, Context.Action.TargetSlot, Command.StatusGroup, Command.StatusBits, 5);
			}
		}

		void DispelBuffs(FBattleContext& Context)
		{
			const FBattleCommand& Command = BattleRules::Command(Context);
			if (Percent(Context) > static_cast<int32>(Command.StatusChance) || Command.StatusGroup != DispelGroup)
			{
				return;
			}
			FBattleCombatant& Target = BattleRules::Target(Context);
			const uint16 Bits = Command.StatusBits;
			if ((Bits & 0x8000) != 0) Target.Buffs = 0;
			if ((Bits & 0x4000) != 0) Target.PermanentBuffs = 0;
			if ((Bits & 0x2000) != 0) Target.EtherBuffs = 0;
			if ((Bits & 0x1000) != 0) Target.PermanentEtherBuffs = 0;
			if ((Bits & 0x0800) != 0) Target.Elements = 0;
			if ((Bits & 0x0400) != 0) Target.PermanentElements = 0;
			Context.Action.Message = 0x3A;
		}
	}
}
