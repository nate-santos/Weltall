#include "Battle/BattleGameState.h"
#include "Battle/BattleRules.h"
#include "Battle/BattleSteps.h"
#include "Foundation/ProcessorDivide.h"

#include <algorithm>

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint8 Billy = 4;

		bool IsEther(const FBattleCommand& Command)
		{
			return (Command.Flags & BattleCommandFlags::Ether) != 0;
		}

		/** Half of a value read as 16 bits signed, rounded toward zero, as the game halves hit and evade. */
		uint32 SignedHalf(uint32 Value)
		{
			const int32 Signed = static_cast<int16>(Value);
			return static_cast<uint32>((Signed - (Signed < 0 ? -1 : 0)) >> 1);
		}

		/** The same halving done with a logical shift: a negative value becomes about 2^31. */
		uint32 UnsignedHalf(uint32 Value)
		{
			const int32 Signed = static_cast<int16>(Value);
			return static_cast<uint32>(Signed - (Signed < 0 ? -1 : 0)) >> 1;
		}

		/** A gear's ammunition count, or none (a fault) when it would lie past the game state. */
		const uint8* GearAmmunition(FBattleContext& Context, uint8 Index)
		{
			const std::size_t Offset = GameStateLayout::StoryFlagsOffset + Index;
			if (Offset >= FGameState::BlockSize)
			{
				Context.Fault = EBattleFault::OutsideGameState;
				return nullptr;
			}
			return &Context.GameState->Block()[Offset];
		}

		/** A gear's resistance to the command's kind of damage (its highest bit), in twentieths. */
		int32 ApplyResistance(FBattleContext& Context, int32 Damage)
		{
			const uint16 Kind = BattleRules::Command(Context).DamageKind;
			std::size_t Index = 0;
			while ((Kind & (0x8000u >> Index)) == 0)
			{
				++Index;
			}
			const uint32 Resistance = TargetGear(Context).Resistances[Index];
			if (Resistance == 0)
			{
				return Damage;
			}
			return Resistance < 20 ? Damage * static_cast<int32>(20 - Resistance) / 20 : 0;
		}
	}

	namespace BattleSteps
	{
		uint8 GearRollHit(FBattleContext& Context)
		{
			const FBattleCommand& Command = BattleRules::Command(Context);
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			const FBattleCombatant& Target = BattleRules::Target(Context);
			const FBattleGearPart& Gear = ActorGear(Context);
			const FBattleGearPart& TargetPart = TargetGear(Context);
			if ((Command.Flags & BattleCommandFlags::MissesShielded) != 0 && (Target.Traits & 8) != 0)
			{
				return 3;
			}
			if ((Command.Flags & BattleCommandFlags::AlwaysMisses) != 0 || ((Target.Buffs | Target.PermanentBuffs) & 0x100) != 0)
			{
				return 3;
			}
			if ((Target.Status & 0x2000) != 0 || (Target.Mental & 0x1000) != 0 || (Command.Flags & BattleCommandFlags::NeverMisses) != 0)
			{
				return 1;
			}
			uint32 Evade = Target.Evade;
			const uint8* HitSwitch = GearAmmunition(Context, Gear.AmmunitionIndex0);
			if (HitSwitch == nullptr)
			{
				return 3;
			}
			uint32 Hit = Actor.Hit;
			if (*HitSwitch == 0)
			{
				Hit = static_cast<uint32>(static_cast<int32>(Gear.HitBonus)) + Hit;
			}
			const int32 TargetBonus = TargetPart.HitBonus;
			if (TargetBonus != 0)
			{
				Evade = (static_cast<uint32>(TargetBonus - (TargetBonus < 0 ? -1 : 0)) >> 1) + Evade;
			}
			if (Actor.Character == Billy)
			{
				if ((Command.WeaponSlots & 0x80) != 0 && *HitSwitch == 0)
				{
					return 3;
				}
				if ((Command.WeaponSlots & 0x20) != 0)
				{
					const uint8* BigGun = GearAmmunition(Context, Gear.AmmunitionIndex2);
					if (BigGun == nullptr || *BigGun == 0)
					{
						return 3;
					}
				}
			}
			if (((Gear.Buffs | Gear.PermanentBuffs) & 0x800) != 0)
			{
				Hit = Hit + SignedHalf(Hit);
			}
			const int32 Clumsy = (Gear.Status & 0x10) != 0 ? 60 : 0;
			int32 Exposed = 0;
			if (!InGear(Context, Context.Action.TargetSlot))
			{
				Hit = UnsignedHalf(Hit);
				if ((Target.Status & 0x2000) != 0 || (Target.Mental & 0x1000) != 0)
				{
					return 1;
				}
				if (((Target.Buffs | Target.PermanentBuffs) & 0x800) != 0)
				{
					Evade = Evade + SignedHalf(Evade);
				}
			}
			else
			{
				if (((TargetPart.Buffs | TargetPart.PermanentBuffs) & 0x400) != 0)
				{
					Evade = Evade + SignedHalf(Evade);
				}
				if ((TargetPart.Status & 0x10) != 0)
				{
					Exposed = 30;
				}
				if ((TargetPart.Status & 0xC00) != 0)
				{
					return 1;
				}
			}
			if ((Context.Combatants[Context.Action.TargetSlot].BattleFlags & BattleFlags::Defending) != 0)
			{
				return Percent(Context) > 94 ? 1 : 2;
			}
			const uint32 HitWindow = (Hit + static_cast<uint32>(static_cast<int32>(Command.HitBonus))) - Evade;
			auto Roll = [&Context, HitWindow]() { return static_cast<int16>(static_cast<uint32>(Percent(Context)) - HitWindow); };
			const uint16 TargetBuffs = Target.Buffs | Target.PermanentBuffs;
			if ((TargetBuffs & 0x20) != 0)
			{
				return Roll() < 50 ? 1 : 3;
			}
			if ((TargetBuffs & 0x40) != 0)
			{
				return Roll() < 50 ? 1 : 2;
			}
			const int16 Threshold = static_cast<int16>(Exposed + 85 - Clumsy);
			if (Roll() < Threshold)
			{
				return Threshold <= Roll() ? 2 : 1;
			}
			return 3;
		}

		uint16 GearCalcAttack(FBattleContext& Context)
		{
			uint32 Attack = static_cast<uint32>(static_cast<int32>(CalcAttack(Context)));
			if (IsEther(BattleRules::Command(Context)))
			{
				const FBattleGearPart& Gear = ActorGear(Context);
				uint32 Amplifier = Gear.EtherAmplifier;
				if (Context.bPerfectWorksRebalance)
				{
					// Perfect Works' rebalance halves the amplifier for the status, then keeps it at 4 at least.
					Amplifier = std::max<uint32>((Gear.Status & 0x100) != 0 ? Amplifier >> 1 : Amplifier, 4);
				}
				else if ((Gear.Status & 0x100) != 0 || Amplifier == 0)
				{
					Amplifier = 4;
				}
				Attack = static_cast<uint32>(static_cast<int32>(Amplifier * (Attack & 0xFFFF)) >> 2);
				if (((Gear.Buffs | Gear.PermanentBuffs) & 0x40) != 0)
				{
					Attack = Attack + ((Attack & 0xFFFF) >> 1);
				}
			}
			return static_cast<uint16>(Attack);
		}

		uint16 GearCalcDefense(FBattleContext& Context)
		{
			uint32 Defense = static_cast<uint32>(static_cast<int32>(CalcDefense(Context)));
			const FBattleGearPart& Gear = TargetGear(Context);
			if (!IsEther(BattleRules::Command(Context)))
			{
				if (Gear.ArmorBreak != 0)
				{
					Defense = static_cast<uint32>(static_cast<int32>((Defense & 0xFFFF) * (100u - Gear.ArmorBreak)) / 100);
				}
			}
			else
			{
				uint32 EtherDefense = Gear.EtherDefense;
				if (Gear.ArmorBreak != 0 && EtherDefense != 0)
				{
					EtherDefense = static_cast<uint32>(static_cast<int32>(EtherDefense * (100u - Gear.ArmorBreak)) / 100);
				}
				if (InGear(Context, Context.Action.TargetSlot))
				{
					Defense += EtherDefense;
				}
				// Perfect Works' rebalance moves the buff to 80h.
				if (((Gear.Buffs | Gear.PermanentBuffs) & (Context.bPerfectWorksRebalance ? 0x80 : 0x20)) != 0)
				{
					Defense = Defense + ((Defense & 0xFFFF) >> 1);
				}
			}
			return static_cast<uint16>(Defense);
		}

		void GearDamage(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			FBattleCombatant& Actor = BattleRules::Actor(Context);
			FBattleCombatant& Target = BattleRules::Target(Context);
			const FBattleGearPart& Gear = ActorGear(Context);
			const FBattleGearPart& TargetPart = TargetGear(Context);
			const bool bEther = IsEther(Command);
			int32 Power = Command.Power;
			uint8 Hit = GearRollHit(Context);
			uint16 Attack = GearCalcAttack(Context);
			uint16 Defense = GearCalcDefense(Context);
			ApplyElement(Context, Attack, Defense, Hit);
			// Perfect Works' rebalance reads the buffs without their permanent halves and moves the tenths taken off to
			// 10h and 20h.
			const bool bRebalance = Context.bPerfectWorksRebalance;
			const uint16 GearBuffs = bRebalance ? Gear.Buffs : static_cast<uint16>(Gear.Buffs | Gear.PermanentBuffs);
			if ((GearBuffs & 8) != 0) Attack = static_cast<uint16>(Attack + Attack / 5);
			if ((GearBuffs & 2) != 0) Attack = static_cast<uint16>(Attack + Attack / 10);
			if ((GearBuffs & 4) != 0) Attack = static_cast<uint16>(Attack - Attack / 5);
			if ((GearBuffs & (bRebalance ? 0x10 : 1)) != 0) Attack = static_cast<uint16>(Attack - Attack / 10);
			const uint16 TargetBuffs = bRebalance ? TargetPart.Buffs : static_cast<uint16>(TargetPart.Buffs | TargetPart.PermanentBuffs);
			if ((TargetBuffs & 4) != 0) Defense = static_cast<uint16>(Defense + Defense / 5);
			if ((TargetBuffs & 1) != 0) Defense = static_cast<uint16>(Defense + Defense / 10);
			if ((TargetBuffs & 8) != 0) Defense = static_cast<uint16>(Defense - Defense / 5);
			if ((TargetBuffs & (bRebalance ? 0x20 : 2)) != 0) Defense = static_cast<uint16>(Defense - Defense / 10);
			if ((Command.Element & 0x10) != 0)
			{
				if ((Target.MentalImmunity & 0x40) == 0)
				{
					Target.Mental |= 0x40;
				}
				const uint16 GearElements = Gear.Elements | Gear.PermanentElements;
				if ((GearElements & 0x4000) != 0)
				{
					Action.Results[Action.ActorSlot] = BattleResults::EpHeal;
					Action.Amounts[Action.ActorSlot] = static_cast<uint32>(Actor.MaxEp / 10) << 1;
				}
				if ((GearElements & 0x1000) != 0)
				{
					Action.Results[Action.ActorSlot] = BattleResults::HpHeal;
					Action.Amounts[Action.ActorSlot] = Gear.MaxHp / 10 << 1;
				}
			}
			if ((Command.Element & 0x20) != 0 && (Target.MentalImmunity & 0x80) == 0)
			{
				Target.Mental |= 0x80;
			}
			if ((Target.Mental & 0x40) != 0)
			{
				Defense = static_cast<uint16>(Defense - (Defense >> 2));
				Target.Mental &= 0xFFBF;
			}
			if ((Actor.Mental & 0x80) != 0)
			{
				Attack = static_cast<uint16>(Attack - (Attack >> 2));
				Actor.Mental &= 0xFF7F;
			}
			if ((Command.Flags & BattleCommandFlags::Deathblow) != 0)
			{
				Power = 20;
			}
			const int32 AttackScale = bEther ? 5 : 4;
			const int32 DefenseScale = bEther ? 4 : 3;
			int32 Damage = 0;
			if (bRebalance)
			{
				// Its base adds the command's own power and takes an eighth of the attack off.
				Damage = AttackScale * Attack - (Attack >> 3) + Command.Power;
				if (Defense != 0)
				{
					Damage -= DefenseScale * Defense;
				}
			}
			else
			{
				Damage = Defense == 0 ? AttackScale * Attack : AttackScale * Attack - DefenseScale * Defense;
			}
			if (Command.AttackFormula < 2)
			{
				Damage = Power * Damage / 20;
			}
			if (Damage < 1)
			{
				Damage = 0;
			}
			else if (Damage < 15)
			{
				Damage += Draw(Context) % 3;
			}
			else
			{
				Damage += Draw(Context) % (Damage / 15 + 2);
			}
			if (Command.DamageKind != 0)
			{
				Damage = ApplyResistance(Context, Damage);
			}
			switch (Hit)
			{
			case 1:
				Action.Results[Action.TargetSlot] = BattleResults::HpDamage;
				break;
			case 2:
				Action.Results[Action.TargetSlot] = BattleResults::HalvedDamage;
				if (Damage != 0)
				{
					Damage = Damage * static_cast<int32>(10 - std::min<uint32>(TargetPart.Guard, 9)) / 20;
				}
				break;
			case 3:
				Damage = 0;
				Action.Results[Action.TargetSlot] = BattleResults::Miss;
				break;
			case 4:
				Action.Results[Action.TargetSlot] = BattleResults::HpHeal;
				break;
			default:
				break;
			}
			if (Action.bEtherFailed && bEther && Damage != 0)
			{
				// Perfect Works' rebalance: Elly's critical hit doubles it.
				Damage = bRebalance ? Damage * 2 : Damage / 3;
			}
			Action.Amounts[Action.TargetSlot] = static_cast<uint32>(std::clamp(Damage, 0, 9999));
		}
	}
}
