#include "Battle/BattleGameState.h"
#include "Battle/BattleRules.h"
#include "Battle/BattleSteps.h"

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint8 Fei = 0;
		constexpr uint8 Elly = 1;
		constexpr uint8 Billy = 4;
		constexpr uint8 Renmazuo = 5;
		constexpr uint8 Omnigear = 13;

		void ClearArmorBreak(FBattleContext& Context)
		{
			TargetGear(Context).ArmorBreak = 0;
		}

		void Repair(FBattleContext& Context)
		{
			const FBattleGearPart& Gear = TargetGear(Context);
			Context.Action.Results[Context.Action.TargetSlot] = BattleResults::HpHeal;
			Context.Action.Amounts[Context.Action.TargetSlot] = static_cast<uint32>(Gear.RepairLevel) * Gear.MaxHp / 10;
		}

		void HealByPower(FBattleContext& Context)
		{
			Context.Action.Results[Context.Action.TargetSlot] = BattleResults::HpHeal;
			Context.Action.Amounts[Context.Action.TargetSlot] = static_cast<uint32>(BattleRules::Command(Context).Power) * TargetGear(Context).MaxHp / 20;
		}

		void StartInfinity(FBattleContext& Context)
		{
			Combatant(Context, Context.Action.TargetFields).AttackLevel = 4;
			Context.Combatants[Context.Action.TargetSlot].Durations[6] = 3;
		}

		void DrainFuel(FBattleContext& Context)
		{
			Context.Action.Results[Context.Action.TargetSlot] = BattleResults::FuelLoss;
			Context.Action.Amounts[Context.Action.TargetSlot] = static_cast<uint32>(TargetGear(Context).MaxFuel) * BattleRules::Command(Context).Power / 20;
		}

		void RestoreFuel(FBattleContext& Context)
		{
			Context.Action.Results[Context.Action.TargetSlot] = BattleResults::FuelGain;
			Context.Action.Amounts[Context.Action.TargetSlot] = static_cast<uint32>(TargetGear(Context).MaxFuel) * BattleRules::Command(Context).Power / 20;
		}

		void CureStatus(FBattleContext& Context)
		{
			TargetGear(Context).Status &= 0xF80B;
			BattleRules::Target(Context).DisabledCommands &= 0xFFDF;
		}

		void HealByEther(FBattleContext& Context)
		{
			Context.Action.Results[Context.Action.TargetSlot] = BattleResults::HpHeal;
			Context.Action.Amounts[Context.Action.TargetSlot] = static_cast<uint32>(BattleRules::Actor(Context).Ether) * BattleRules::Command(Context).Power;
		}

		void StatusOnly(FBattleContext& Context)
		{
			if (!BattleSteps::GearApplyStatus(Context, false))
			{
				Context.Action.Results[Context.Action.TargetSlot] = BattleResults::NoEffect;
			}
		}

		using FHandler = void (*)(FBattleContext&);
		constexpr std::array<FHandler, 11> GearHandlers{
			&BattleSteps::GearDamage,
			&StatusOnly,
			&ClearArmorBreak,
			&Repair,
			&HealByPower,
			&StartInfinity,
			&DrainFuel,
			&RestoreFuel,
			&CureStatus,
			&BattleSteps::HandlerFixedDamage,
			&HealByEther,
		};

		/** A plain or an Infinity attack aimed at someone on foot takes its on-foot form, and keeps it for the targets after. */
		void RedirectCommand(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			if (InGear(Context, Action.TargetSlot))
			{
				return;
			}
			if (static_cast<uint8>(Action.Command - 12) < 3)
			{
				Action.CommandIndex = static_cast<uint16>(Action.CommandIndex + 3);
			}
			else if (Action.Command < 3)
			{
				Action.CommandIndex = static_cast<uint16>(Action.CommandIndex + 15);
			}
		}

		/** The party's gear actions move the attack level, and a gear Deathblow counts toward Infinity. */
		void AfterHit(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const uint8 Command = Action.Command;
			if (FBattleContext::IsParty(Action.ActorSlot))
			{
				FBattleCombatant& Fields = Combatant(Context, Action.ActorFields);
				if (Command < 3)
				{
					const uint8 Raised = static_cast<uint8>(Fields.AttackLevel + 1);
					Fields.AttackLevel = Raised;
					if (Fields.MaxAttackLevel < Raised)
					{
						Fields.AttackLevel = static_cast<uint8>(Fields.AttackLevel - 1);
					}
				}
				if (Command >= 3 && Command <= 11)
				{
					Fields.AttackLevel = static_cast<uint8>(Context.GearTurn.Level - (Command / 3));
				}
				FBattleCombatant& Actor = BattleRules::Actor(Context);
				if (Command >= 3 && Command <= 11)
				{
					// Perfect Works' rebalance counts the Deathblow's number twice instead of its third.
					Actor.InfinityCounter = static_cast<uint8>(Actor.InfinityCounter + (Context.bPerfectWorksRebalance ? Command * 2 : Command / 3));
				}
				if (Command == 5 || Command == 8 || Command == 11)
				{
					Actor.InfinityCounter = static_cast<uint8>(Actor.InfinityCounter + Command / 3);
				}
			}
			FBattleCombatant& Target = BattleRules::Target(Context);
			const uint8 TargetSlot = Action.TargetSlot;
			if (Action.Results[TargetSlot] == BattleResults::HpDamage && (Target.Mental & 0x2000) != 0 && Percent(Context) < 80)
			{
				Target.Mental &= 0xDFFF;
				TargetGear(Context).Status &= 0xEFFF;
			}
			if (Action.Results[TargetSlot] == BattleResults::FuelLoss && (TargetGear(Context).StatusImmunity & 0x80) != 0)
			{
				Action.Amounts[TargetSlot] = 0;
			}
			if (Action.Results[TargetSlot] == BattleResults::HpDamage)
			{
				if ((Target.EquipmentFlags & 0x80) != 0)
				{
					const int32 Chance = Target.Character == Fei ? 80 : 60;
					uint32& Amount = Action.Amounts[TargetSlot];
					Amount = Percent(Context) < Chance ? Amount >> 1 : (Amount >> 1) + Amount;
				}
				if ((Target.EquipmentFlags & 0x20) != 0)
				{
					Action.Results[Action.ActorSlot] = BattleResults::HpDamage;
					Action.Amounts[Action.ActorSlot] = Action.Amounts[TargetSlot];
				}
			}
			if ((Target.PermanentEtherBuffs & 0x200) != 0 && Action.Results[TargetSlot] == BattleResults::EpDamage)
			{
				Action.Amounts[TargetSlot] = 0;
			}
		}

		/** A gear's evasion buff shows on its pilot's buffs. */
		void SyncDodgeFlag(FBattleContext& Context, uint8 Slot)
		{
			FBattleCombatant& Combatant = Context.Combatants[Slot];
			if ((Combatant.GearPart.Buffs & 0x200) != 0)
			{
				Combatant.Buffs |= 0x20;
			}
			else
			{
				Combatant.Buffs &= 0xFFDF;
			}
		}

		/** Billy's gears' shots use up the ammunition of weapon slot 0, 2, or both for a Deathblow. */
		void UseGearAmmunition(FBattleContext& Context)
		{
			const FBattleGearPart& Gear = ActorGear(Context);
			auto Use = [&Context](uint8 Index)
			{
				const std::size_t Offset = GameStateLayout::StoryFlagsOffset + Index;
				if (Offset >= FGameState::BlockSize)
				{
					Context.Fault = EBattleFault::OutsideGameState;
					return;
				}
				uint8& Count = Context.GameState->Block()[Offset];
				if (Count != 0)
				{
					--Count;
				}
			};
			const uint8 Command = Context.Action.Command;
			if (Command == 0 || Command == 15)
			{
				Use(Gear.AmmunitionIndex0);
			}
			else if (Command == 2 || Command == 17)
			{
				Use(Gear.AmmunitionIndex2);
			}
			else if (Command >= 3 && Command <= 14)
			{
				Use(Gear.AmmunitionIndex0);
				Use(Gear.AmmunitionIndex2);
			}
		}

		/** The command's display bytes and number for the show, with the gear's element when it has none. */
		void SetActionInfo(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			Action.Info = {Command.Category, Command.Info21, Command.Element, Command.Info23};
			Action.InfoCommand = Action.Command;
			if ((Action.Info[2] & 0x3F) == 0)
			{
				const uint8 High = static_cast<uint8>((ActorGear(Context).Elements >> 8) | (BattleRules::Actor(Context).PermanentEtherBuffs >> 8));
				Action.Info[2] = static_cast<uint8>(Action.Info[2] | (High >> 4));
			}
		}
	}

	namespace BattleSteps
	{
		void GearAct(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			Action.CommandIndex = FBattleContext::IsParty(Action.ActorSlot) ? FBattleContext::GearCommandIndex(Action.Actor, Action.Command)
																		 : FBattleContext::EnemyCommandIndex(Action.Command);
			if (!FBattleContext::IsLoadedCommand(Action.CommandIndex))
			{
				Context.Fault = EBattleFault::CommandOutsideTable;
				return;
			}
			ResetEtherTargetAll(Context, Action.ActorSlot, true);
			Action.bEtherFailed = false;
			Action.ActorFields = Action.Actor;
			FBattleCombatant& Actor = BattleRules::Actor(Context);
			if ((BattleRules::Command(Context).Flags & BattleCommandFlags::Ether) != 0 && Actor.Character == Elly)
			{
				RollEtherFailure(Context);
			}
			if ((Actor.Gear == Renmazuo || Actor.Gear == Omnigear) && Action.Command == 1)
			{
				BattleRules::Command(Context).Element = ActorGear(Context).Weapons[1].Attack;
			}
			uint32 Bit = 1;
			Action.TargetSlot = 0;
			do
			{
				if ((Bit & Action.Targets) != 0)
				{
					PointTargetAt(Context, Action.TargetSlot);
					RedirectCommand(Context);
					const uint8 Handler = BattleRules::Command(Context).Handler;
					if (Context.Fault != EBattleFault::None)
					{
						return;
					}
					if (Handler >= GearHandlers.size())
					{
						Context.Fault = EBattleFault::UnknownHandler;
						return;
					}
					GearHandlers[Handler](Context);
					if (Action.Results[Action.TargetSlot] == BattleResults::HpDamage)
					{
						const uint16 Flags = BattleRules::Command(Context).Flags;
						// Perfect Works' rebalance gives the command's own status before the weapon's.
						const bool bOwnFirst = Context.bPerfectWorksRebalance && (Flags & BattleCommandFlags::StatusOnHit) != 0;
						if ((Flags & BattleCommandFlags::WeaponStatus) != 0 && !bOwnFirst)
						{
							GearApplyStatus(Context, true);
						}
						else if ((Flags & BattleCommandFlags::StatusOnHit) != 0)
						{
							GearApplyStatus(Context, false);
						}
					}
					Action.Animation = BattleRules::Command(Context).Animation;
					AfterHit(Context);
					SyncDodgeFlag(Context, Action.TargetSlot);
				}
				Action.TargetSlot = static_cast<uint8>(Action.TargetSlot + 1);
				Bit = (Bit << 1) & 0xFFFF;
			} while (Action.TargetSlot < FBattleContext::SlotCount);
			if (BattleRules::Actor(Context).Character == Billy)
			{
				UseGearAmmunition(Context);
			}
			SetActionInfo(Context);
		}
	}
}
