#include "Battle/BattleAction.h"

#include "Battle/BattleGameState.h"
#include "Battle/BattleRules.h"
#include "Battle/BattleSteps.h"

#include <optional>

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint8 Fei = 0;
		constexpr uint8 Elly = 1;
		constexpr uint8 Bart = 3;
		constexpr uint8 Billy = 4;
		/** The party's record 20: the counterattack. */
		constexpr int32 CounterCommand = 20;
		constexpr uint8 CounterMessage = 0x34;
		constexpr uint16 PlayDead = 0x0002;

		using FHandler = void (*)(FBattleContext&);
		constexpr std::array<FHandler, 8> Handlers{
			&BattleSteps::HandlerDamage,
			&BattleSteps::HandlerHeal,
			&BattleSteps::HandlerStatus,
			&BattleSteps::HandlerDrain,
			&BattleSteps::HandlerFixedDamage,
			&BattleSteps::HandlerCure,
			&BattleSteps::HandlerRevive,
			&BattleSteps::HandlerRestoreGearHp,
		};

		/**
		 * A party target with Counter UP may strike back: it becomes the actor, with its own
		 * counterattack, and the actor its target (the gear parts and the target's battle fields the
		 * rules point at stay where they were).
		 */
		void TryCounter(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCombatant& Target = BattleRules::Target(Context);
			if ((Target.Status & 0xA000) != 0 || (Target.Mental & 0x1000) != 0)
			{
				return;
			}
			const FBattleCommand& Command = BattleRules::Command(Context);
			if ((Action.ActorSlot < FBattleContext::PartySlotCount && (Target.Mental & 0x2000) == 0)
				|| (Target.EtherBuffs & 0x1000) == 0
				|| (Command.Flags & (BattleCommandFlags::Uncounterable | BattleCommandFlags::Ether)) != 0
				|| InGear(Context, Action.ActorSlot))
			{
				return;
			}
			const int32 Chance = Target.Character == Fei ? 60 : 50;
			if (Percent(Context) > Chance || Command.Handler == 2)
			{
				return;
			}
			const uint8 Counterer = Action.TargetSlot;
			Action.TargetSlot = Action.ActorSlot;
			Action.ActorSlot = Counterer;
			std::swap(Action.ActorCombatant, Action.TargetCombatant);
			Action.CommandIndex = FBattleContext::PartyCommandIndex(Counterer, CounterCommand);
			Action.Results[Counterer] = BattleResults::Counterattack;
			Action.Amounts[Counterer] = 0;
			Action.Message = CounterMessage;
		}

		/** Who acts on whom, with what: what a counter swaps. */
		struct FActionRoles
		{
			uint8 ActorSlot = 0;
			uint8 TargetSlot = 0;
			uint8 ActorCombatant = 0;
			uint8 TargetCombatant = 0;
			uint16 CommandIndex = 0;
			uint16 Animation = 0;

			static FActionRoles Of(const FBattleAction& Action)
			{
				return {Action.ActorSlot, Action.TargetSlot, Action.ActorCombatant, Action.TargetCombatant, Action.CommandIndex, Action.Animation};
			}

			void Restore(FBattleAction& Action) const
			{
				Action.ActorSlot = ActorSlot;
				Action.TargetSlot = TargetSlot;
				Action.ActorCombatant = ActorCombatant;
				Action.TargetCombatant = TargetCombatant;
				Action.CommandIndex = CommandIndex;
				Action.Animation = Animation;
			}
		};

		/** What follows each target: its equipment's and statuses' reactions to the result. */
		void ApplyTargetEffects(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const uint8 TargetSlot = Action.TargetSlot;
			const uint8 ActorSlot = Action.ActorSlot;
			FBattleCombatant& Target = BattleRules::Target(Context);
			if (Action.Results[TargetSlot] == BattleResults::HpDamage)
			{
				if (Action.Amounts[TargetSlot] == Target.Hp && FBattleContext::IsParty(ActorSlot))
				{
					FBattleCombatant& Actor = BattleRules::Actor(Context);
					const uint16 Kills = static_cast<uint16>(Actor.ExactKills + 1);
					if (Kills <= 65000)
					{
						Actor.ExactKills = Kills;
					}
				}
				const uint16 Mental = Target.Mental;
				Target.Mental = Mental & 0xEFFF;
				if ((Mental & 0x2000) != 0 && Percent(Context) < 70)
				{
					Target.Mental &= 0xDFFF;
				}
				if ((BattleRules::Command(Context).Flags & BattleCommandFlags::Ether) != 0)
				{
					if ((Target.Absorption & 0x4000) != 0)
					{
						Action.Results[TargetSlot] = BattleResults::HpHeal;
					}
					if ((Target.Absorption & 0x2000) != 0 && (BattleRules::Command(Context).Element & 0xF) == 0)
					{
						Action.Results[TargetSlot] = BattleResults::HpHeal;
					}
				}
				if ((Target.EquipmentFlags & 0x80) != 0)
				{
					const int32 Chance = Target.Character == Fei ? 80 : 60;
					uint32& Amount = Action.Amounts[TargetSlot];
					Amount = Percent(Context) < Chance ? Amount >> 1 : (Amount >> 1) + Amount;
				}
				if ((Target.EquipmentFlags & 0x20) != 0)
				{
					Action.Results[ActorSlot] = BattleResults::HpDamage;
					Action.Amounts[ActorSlot] = Action.Amounts[TargetSlot];
				}
				if (FBattleContext::IsParty(ActorSlot) && ActorSlot != TargetSlot && (Context.Combatants[TargetSlot].Status & PlayDead) != 0)
				{
					if (TargetSlot >= FBattleContext::PartySlotCount)
					{
						Context.Fault = EBattleFault::SlotOutsideBattle;
						return;
					}
					FBattleCombatant& PlayingDead = Context.Combatants[TargetSlot];
					PlayingDead.Status &= static_cast<uint16>(~PlayDead);
					PlayingDead.DisabledCommands = Context.StartingDisabledCommands[TargetSlot];
				}
				if (Target.Character == Bart && Target.Hp <= Action.Amounts[TargetSlot])
				{
					for (std::size_t Slot = FBattleContext::PartySlotCount; Slot < FBattleContext::SlotCount; ++Slot)
					{
						Context.Combatants[Slot].Mental &= 0xFFDF;
					}
				}
			}
			if ((Target.Absorption & 0x8000) != 0)
			{
				uint8& Result = Action.Results[TargetSlot];
				if (Result == BattleResults::HpHeal)
				{
					Result = BattleResults::HpDamage;
				}
				else if (Result == BattleResults::HpDamage || Result == BattleResults::HalvedDamage)
				{
					Result = BattleResults::HpHeal;
				}
			}
			if ((Target.PermanentBuffs & 0x80) != 0 && Action.Results[TargetSlot] == BattleResults::HpHeal)
			{
				Action.Amounts[TargetSlot] <<= 1;
			}
			if ((Target.PermanentEtherBuffs & 0x200) != 0 && Action.Results[TargetSlot] == BattleResults::EpDamage)
			{
				Action.Amounts[TargetSlot] = 0;
			}
		}

		/** Perfect Works' rebalance after a landed hit: the weapon's status, then the command's own, or its dispel, or its status on the actor. */
		void ApplyPerfectWorksHitEffects(FBattleContext& Context, uint16 Flags)
		{
			if ((Flags & BattleCommandFlags::WeaponStatus) != 0)
			{
				BattleSteps::ApplyWeaponStatus(Context);
			}
			if ((Flags & BattleCommandFlags::StatusOnHit) != 0)
			{
				BattleSteps::HandlerStatus(Context);
			}
			else if ((Flags & BattleCommandFlags::Dispels) != 0)
			{
				BattleSteps::DispelBuffs(Context);
			}
			else if ((Flags & BattleCommandFlags::PerfectWorksSelfStatus) != 0)
			{
				// The actor takes the target's place for it, its turns counted on its own slot.
				FBattleAction& Action = Context.Action;
				const uint8 Target = Action.TargetCombatant;
				const uint8 TargetSlot = Action.TargetSlot;
				Action.TargetCombatant = Action.ActorCombatant;
				Action.TargetSlot = Action.ActorSlot;
				BattleSteps::HandlerStatus(Context);
				Action.TargetCombatant = Target;
				Action.TargetSlot = TargetSlot;
			}
		}

		/** The command's display bytes and number for the show, with the actor's own element when it has none. */
		void StoreActionElement(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			Action.Info = {Command.Category, Command.Info21, Command.Element, Command.Info23};
			Action.InfoCommand = Action.Command;
			if ((Action.Info[2] & 0x3F) == 0)
			{
				const uint8 High = static_cast<uint8>((Actor.Elements >> 8) | (Actor.PermanentElements >> 8));
				Action.Info[2] = static_cast<uint8>(Action.Info[2] | (High >> 4));
			}
		}

		/** Billy's shots use up his guns' ammunition: the pistol's for his weak shots, the big gun's for the fierce, both for a Deathblow. */
		void ConsumeAmmunition(FBattleContext& Context)
		{
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			auto Use = [&Context](uint8 Kind)
			{
				uint8* Count = BattleGameState::AmmunitionCount(*Context.GameState, Kind);
				if (Count == nullptr)
				{
					Context.Fault = EBattleFault::OutsideGameState;
				}
				else if (*Count != 0)
				{
					--*Count;
				}
			};
			const uint8 Command = Context.Action.Command;
			if (Command <= 3)
			{
				Use(Actor.PistolAmmunition);
			}
			else if (Command == 6)
			{
				Use(Actor.BigGunAmmunition);
			}
			else if (Command >= 7 && Command <= 19)
			{
				Use(Actor.PistolAmmunition);
				Use(Actor.BigGunAmmunition);
			}
		}
	}

	void PerformBattleCommand(FBattleContext& Context, uint8 Actor, uint8 Command, uint16 Targets, uint16 Animation)
	{
		FBattleAction& Action = Context.Action;
		Action.KnockedOut = 0;
		Action.LivingAtStart = Context.Living;
		Action.Command = Command;
		Action.Targets = Targets;
		Action.Animation = Animation;
		Action.Actor = Actor;
		ExecuteBattleAction(Context);
	}

	void ExecuteBattleAction(FBattleContext& Context)
	{
		FBattleAction& Action = Context.Action;
		Action.Amounts = {};
		Action.Results.fill(BattleResults::NoResult);
		Action.bNoAmmunition = false;
		Action.ActorSlot = Action.Actor;
		PointActorAt(Context, Action.ActorSlot);
		if (Action.ActorSlot >= FBattleContext::SlotCount)
		{
			Context.Fault = EBattleFault::SlotOutsideBattle;
			return;
		}
		if (InGear(Context, Action.ActorSlot))
		{
			BattleSteps::GearAct(Context);
			BattleSteps::ApplyEtherTargetAll(Context, Action.ActorSlot);
			return;
		}
		Action.CommandIndex = FBattleContext::IsParty(Action.ActorSlot) ? FBattleContext::PartyCommandIndex(Action.ActorSlot, Action.Command)
																		 : FBattleContext::EnemyCommandIndex(Action.Command);
		if (!FBattleContext::IsLoadedCommand(Action.CommandIndex))
		{
			Context.Fault = EBattleFault::CommandOutsideTable;
			return;
		}
		BattleSteps::ResetEtherTargetAll(Context, Action.ActorSlot, true);
		FBattleCommand& Command = BattleRules::Command(Context);
		if ((Command.Flags & BattleCommandFlags::GearAction) != 0)
		{
			BattleSteps::GearAct(Context);
			BattleSteps::ApplyEtherTargetAll(Context, Action.ActorSlot);
			return;
		}
		Action.bEtherFailed = false;
		FBattleCombatant& Actor = BattleRules::Actor(Context);
		if ((Command.Flags & BattleCommandFlags::Ether) != 0 && Actor.Character == Elly)
		{
			BattleSteps::RollEtherFailure(Context);
		}
		if (Actor.Character == Billy && static_cast<uint8>(Action.Command - 4) < 2)
		{
			// Actions 4 and 5 fire his ether guns: the element is their slot's attack byte, which only a reload writes.
			FBattleWeaponSlot& Gun = Actor.Weapons[static_cast<std::size_t>(Action.Command - 3)];
			const std::size_t Loaded = static_cast<std::size_t>(Actor.EtherAmmunition[static_cast<std::size_t>(Action.Command - 4)] - FBattleTables::FirstAmmunition);
			if (Context.RuleOptions.bEtherAmmunitionElement && Loaded < Context.AmmunitionAttacks.size())
			{
				Gun.Attack = Context.AmmunitionAttacks[Loaded];
			}
			Command.Element = Gun.Attack;
		}
		if ((Actor.Mental & 0x20) != 0 && (Action.Targets & 7) != 0 && Action.Targets < 7)
		{
			for (int32 Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
			{
				if (Context.Combatants[static_cast<std::size_t>(Slot)].Character == Bart)
				{
					Action.Targets = SlotBit(Slot);
				}
			}
		}
		// The original walks the targets with the slot a counter swaps, so after a counter it goes on from the
		// attacker's slot with the counterattack. The fix keeps its own slot and gives the rest of the targets back
		// to the attack, then leaves the action as the counter left it.
		const bool bAttackGoesOn = Context.RuleOptions.bCounterKeepsGroupAttack;
		const FActionRoles Attack = FActionRoles::Of(Action);
		std::optional<FActionRoles> Countered;
		uint32 Bit = 1;
		Action.TargetSlot = 0;
		do
		{
			if ((Bit & Action.Targets) != 0)
			{
				const uint8 Slot = Action.TargetSlot;
				PointTargetAt(Context, Slot);
				if (FBattleContext::IsParty(Slot) && !(bAttackGoesOn && Countered))
				{
					TryCounter(Context);
				}
				const uint8 Handler = BattleRules::Command(Context).Handler;
				if (Handler >= Handlers.size())
				{
					Context.Fault = EBattleFault::UnknownHandler;
					return;
				}
				Handlers[Handler](Context);
				ApplyTargetEffects(Context);
				if (Action.Results[Action.TargetSlot] == BattleResults::HpDamage && Context.bPerfectWorksRebalance)
				{
					ApplyPerfectWorksHitEffects(Context, BattleRules::Command(Context).Flags);
				}
				else if (Action.Results[Action.TargetSlot] == BattleResults::HpDamage)
				{
					const uint16 Flags = BattleRules::Command(Context).Flags;
					if ((Flags & BattleCommandFlags::WeaponStatus) != 0)
					{
						BattleSteps::ApplyWeaponStatus(Context);
					}
					else if ((Flags & BattleCommandFlags::StatusOnHit) != 0)
					{
						BattleSteps::HandlerStatus(Context);
					}
					else if ((Flags & BattleCommandFlags::Dispels) != 0)
					{
						BattleSteps::DispelBuffs(Context);
					}
				}
				const FBattleCommand& Current = BattleRules::Command(Context);
				Action.Animation = (Current.Flags & BattleCommandFlags::OwnAnimation) != 0 ? Current.Animation : Action.Command;
				if (bAttackGoesOn && Action.ActorSlot != Attack.ActorSlot)
				{
					Countered = FActionRoles::Of(Action);
					FActionRoles Resumed = Attack;
					Resumed.TargetSlot = Slot;
					Resumed.Restore(Action);
				}
			}
			Action.TargetSlot = static_cast<uint8>(Action.TargetSlot + 1);
			Bit = (Bit << 1) & 0xFFFF;
		} while (Action.TargetSlot < FBattleContext::SlotCount);
		if (Countered)
		{
			Countered->Restore(Action);
		}
		StoreActionElement(Context);
		BattleSteps::ApplyEtherTargetAll(Context, Action.ActorSlot);
		FBattleCombatant& Performer = BattleRules::Actor(Context);
		if (Performer.Character == Billy && !Action.bNoAmmunition)
		{
			ConsumeAmmunition(Context);
		}
		if (FBattleContext::IsParty(Action.ActorSlot) && Action.Command < 7)
		{
			uint16& Counter = Performer.DeathblowCounters[Action.Command];
			if (Counter < 65000)
			{
				Counter = static_cast<uint16>(Counter + Performer.LearningRate + Performer.LearningBonus);
			}
		}
	}
}
