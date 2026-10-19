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
		constexpr uint8 Fei = 0;
		constexpr uint8 Bart = 3;
		constexpr uint8 Billy = 4;
		constexpr uint8 ChuChu = 7;
		constexpr uint8 Maria = 8;
		constexpr uint8 AdultEmeralda = 10;
		/** Def DOWN in the statuses' word (enemy command 33). */
		constexpr uint16 DefDown = 0x0100;
		/** A gear's PWR loss (its status word) and PWR UP (its buffs). */
		constexpr uint16 GearPowerLoss = 0x0004;
		constexpr uint16 GearPowerUp = 0x1000;

		bool IsEther(const FBattleCommand& Command)
		{
			return (Command.Flags & BattleCommandFlags::Ether) != 0;
		}

		uint8 Ammunition(FBattleContext& Context, uint8 Kind)
		{
			const uint8* Count = BattleGameState::AmmunitionCount(*Context.GameState, Kind);
			if (Count == nullptr)
			{
				Context.Fault = EBattleFault::OutsideGameState;
				return 0;
			}
			return *Count;
		}

		/** A roll against the hit window, kept to 16 bits as the game compares it. */
		int16 Window(int32 Roll, int32 HitWindow)
		{
			return static_cast<int16>(Roll - HitWindow);
		}

		uint16 AddFifth(uint16 Value)
		{
			return static_cast<uint16>(Value + Value / 5);
		}

		uint16 AddQuarter(uint16 Value)
		{
			return static_cast<uint16>(Value + (Value >> 2));
		}

		/** The high word of the processor's signed product. */
		uint32 ProductHigh(uint32 Value, uint32 Factor)
		{
			return static_cast<uint32>(static_cast<uint64>(static_cast<int64>(static_cast<int32>(Value)) * static_cast<int32>(Factor)) >> 32);
		}

		/**
		 * Perfect Works' rebalance's formula 2: the command's power added to x/20, which it takes as the high word of
		 * x × 66670000h, then an eighth of that. A physical command with its ether flag takes the actor's ether for its
		 * weapons.
		 */
		uint16 PerfectWorksFormula2(const FBattleCommand& Command, const FBattleCombatant& Actor, uint32 Weapon, uint32 Base, uint32 Ether)
		{
			constexpr uint32 Twentieth = 0x66670000;
			uint32 Product = 0;
			if (IsEther(Command))
			{
				Product = (Ether & 0xFFFF) * Command.Power;
			}
			else
			{
				const uint32 Powered = (Command.Flags & BattleCommandFlags::PerfectWorksEtherPowered) != 0 ? Actor.Ether : Weapon & 0xFFFF;
				Product = (Powered + (Base & 0xFFFF)) * Command.Power;
			}
			return static_cast<uint16>((ProductHigh(Product, Twentieth) + Command.Power) >> 3);
		}
	}

	namespace BattleSteps
	{
		uint8 RollHit(FBattleContext& Context)
		{
			const FBattleCommand& Command = BattleRules::Command(Context);
			FBattleCombatant& Actor = BattleRules::Actor(Context);
			FBattleCombatant& Target = BattleRules::Target(Context);
			const bool bTargetInGear = InGear(Context, Context.Action.TargetSlot);
			if ((Command.Flags & BattleCommandFlags::MissesGears) != 0 && bTargetInGear)
			{
				return 3;
			}
			if (Actor.Character == Billy
				&& (((Command.WeaponSlots & 0x80) != 0 && Ammunition(Context, Actor.PistolAmmunition) == 0)
					|| ((Command.WeaponSlots & 0x10) != 0 && Ammunition(Context, Actor.BigGunAmmunition) == 0)))
			{
				Context.Action.bNoAmmunition = true;
				return 3;
			}
			if ((Command.Flags & BattleCommandFlags::MissesShielded) != 0 && ((Target.Traits & 8) != 0 || bTargetInGear))
			{
				return 3;
			}
			if ((Command.Flags & BattleCommandFlags::AlwaysMisses) != 0)
			{
				return 3;
			}
			const uint16 TargetBuffs = Target.Buffs | Target.PermanentBuffs;
			if ((TargetBuffs & 0x100) != 0)
			{
				return 3;
			}
			if ((Target.Status & 0x2000) != 0 || (Target.Mental & 0x1000) != 0 || (Command.Flags & BattleCommandFlags::NeverMisses) != 0)
			{
				return 1;
			}
			if ((Command.Flags & BattleCommandFlags::SureHit) != 0)
			{
				return 5;
			}
			int32 Accuracy = (Actor.Status & 0x400) != 0 ? -50 : 0;
			if (((Actor.Buffs | Actor.PermanentBuffs) & 0x1000) != 0)
			{
				Accuracy += 30;
			}
			const int32 Evasion = (TargetBuffs & 0x800) != 0 ? 50 : 0;
			const int32 HitWindow = static_cast<int32>(Actor.Hit) + Command.HitBonus - static_cast<int32>(Target.Evade);
			if ((Context.Combatants[Context.Action.TargetSlot].BattleFlags & BattleFlags::Defending) != 0)
			{
				return Percent(Context) > 94 ? 1 : 2;
			}
			if ((TargetBuffs & 0x20) != 0)
			{
				return Window(Percent(Context), HitWindow) < 50 ? 1 : 3;
			}
			if ((TargetBuffs & 0x40) != 0)
			{
				return Window(Percent(Context), HitWindow) < 50 ? 1 : 2;
			}
			if (Window(Percent(Context), HitWindow) < static_cast<int16>(Accuracy - Evasion + 90))
			{
				return static_cast<int16>(Accuracy - Evasion + 85) <= Window(Percent(Context), HitWindow) ? 2 : 1;
			}
			return 3;
		}

		int16 CalcAttack(FBattleContext& Context)
		{
			const FBattleCommand& Command = BattleRules::Command(Context);
			FBattleCombatant& Actor = BattleRules::Actor(Context);
			const FBattleCombatant& Target = BattleRules::Target(Context);
			const bool bActorInGear = InGear(Context, Context.Action.ActorSlot);
			const bool bEther = IsEther(Command);

			std::array<uint32, 4> Weapons{};
			uint32 Base = 0;
			if (!bActorInGear)
			{
				for (std::size_t Slot = 0; Slot < 4; ++Slot)
				{
					Weapons[Slot] = Actor.Weapons[Slot].Attack;
				}
				Base = Actor.Attack;
			}
			else
			{
				const FBattleGearPart& Gear = ActorGear(Context);
				for (std::size_t Slot = 0; Slot < 3; ++Slot)
				{
					Weapons[Slot] = Gear.Weapons[Slot].Attack;
				}
				Base = static_cast<uint32>(Gear.Output) * Gear.AttackMultiplier;
				if (((Gear.Buffs | Gear.PermanentBuffs) & GearPowerUp) != 0)
				{
					Base += static_cast<uint32>(Gear.Output) * 2;
				}
				if (Context.RuleOptions.bGearPowerLoss && (Gear.Status & GearPowerLoss) != 0)
				{
					Base -= std::min(Base, static_cast<uint32>(Gear.Output) * 2);
				}
			}
			// A weapon slot the actor does not have adds a byte the game never set: only a fault if it counts.
			const bool bUnsetWeapon = (Command.WeaponSlots & 0x08) != 0 || (bActorInGear && (Command.WeaponSlots & 0x10) != 0);
			uint32 Weapon = 0;
			for (std::size_t Slot = 0; Slot < 4; ++Slot)
			{
				if ((Command.WeaponSlots & (0x80 >> Slot)) != 0)
				{
					Weapon += Weapons[Slot];
				}
			}
			if (((Actor.Elements | Actor.PermanentElements) & 1) != 0)
			{
				Weapon += Weapon >> 1;
			}
			uint32 Ether = Actor.Ether;
			if (!bEther)
			{
				if (!bActorInGear)
				{
					const uint16 Buffs = Actor.Buffs | Actor.PermanentBuffs;
					uint32 Scale = (Buffs & 0x2000) != 0 ? 5 : 4;
					if ((Actor.Status & 0x200) != 0)
					{
						--Scale;
					}
					if ((Buffs & 0x400) != 0)
					{
						Scale = Scale + 10 - ProcessorDivideUnsigned(Actor.Hp, Actor.MaxHp / 10u);
					}
					Base = (Base & 0xFFFF) * (Scale & 0xFF) >> 2;
				}
			}
			else
			{
				const uint16 EtherBuffs = Actor.EtherBuffs | Actor.PermanentEtherBuffs;
				uint32 Scale = (EtherBuffs & 0x8000) != 0 ? 5 : 4;
				if ((Actor.Mental & 0x400) != 0)
				{
					--Scale;
				}
				Ether = Ether * (Scale & 0xFF) >> 2;
				if ((EtherBuffs & 0x2000) != 0)
				{
					Ether <<= 1;
				}
			}
			if ((Command.Flags & BattleCommandFlags::WeaponsOnly) != 0)
			{
				Base = 0;
			}
			uint16 Attack = 0;
			switch (Command.AttackFormula)
			{
			case 0:
				Attack = static_cast<uint16>(Weapon + Base);
				if (bUnsetWeapon)
				{
					Context.Fault = EBattleFault::UnsetWeaponSlot;
				}
				break;
			case 1:
				Attack = static_cast<uint16>(Ether);
				break;
			case 2:
				if (Actor.Character == Billy)
				{
					Weapon = static_cast<uint32>(static_cast<int32>(Weapon * 6) / 10);
				}
				if (Context.bPerfectWorksRebalance)
				{
					Attack = PerfectWorksFormula2(Command, Actor, Weapon, Base, Ether);
				}
				else if (!bEther)
				{
					Attack = static_cast<uint16>(static_cast<int32>((Weapon + (Base & 0xFFFF)) * Command.Power) / 20);
					if (bUnsetWeapon)
					{
						Context.Fault = EBattleFault::UnsetWeaponSlot;
					}
				}
				else
				{
					Attack = static_cast<uint16>(static_cast<int32>(Ether * Command.Power) / 20);
				}
				break;
			default:
				Context.Fault = EBattleFault::UnknownFormula;
				break;
			}
			if (FBattleContext::IsParty(Context.Action.ActorSlot))
			{
				if (Actor.Character == ChuChu)
				{
					ChuChuPowerBonus(Context, Attack);
				}
				if (Actor.Character == Billy || (Command.Element & 0x20) != 0)
				{
					if ((Target.Weakness & 0x20) != 0)
					{
						Attack = AddQuarter(Attack);
					}
					if ((Target.Weakness & 0x60) == 0x60)
					{
						Attack = AddQuarter(Attack);
					}
				}
				if (Actor.Character == Maria && !bActorInGear && bEther)
				{
					const uint32 Amplifier = Context.Combatants[Context.Action.ActorSlot].GearPart.EtherAmplifier;
					Attack = static_cast<uint16>(static_cast<int32>(Attack * Amplifier) >> 2);
				}
				if (Actor.Character == AdultEmeralda)
				{
					Attack = AddFifth(Attack);
				}
				if ((Command.Element & 0x10) != 0 && (Target.Weakness & 0x10) != 0)
				{
					Attack = AddQuarter(Attack);
					Attack = AddQuarter(Attack);
				}
			}
			return static_cast<int16>(Attack);
		}

		int16 CalcDefense(FBattleContext& Context)
		{
			const FBattleCommand& Command = BattleRules::Command(Context);
			const FBattleCombatant& Target = BattleRules::Target(Context);
			const bool bTargetInGear = InGear(Context, Context.Action.TargetSlot);
			const bool bEther = IsEther(Command);
			uint32 Defense = 0;
			uint32 Armor = 0;
			if (!bTargetInGear)
			{
				Defense = Target.Defense;
				Armor = Target.EquipmentBonuses[5];
			}
			else
			{
				const FBattleGearPart& Gear = TargetGear(Context);
				Armor = Gear.Defense;
				// Perfect Works' rebalance halves a gear's armor for its buff 2000h.
				if (Context.bPerfectWorksRebalance && ((Gear.Buffs | Gear.PermanentBuffs) & 0x2000) != 0)
				{
					Armor -= Armor >> 1;
				}
			}
			uint32 EtherDefense = Target.EtherDefense;
			if (Context.bPerfectWorksRebalance)
			{
				// Perfect Works' rebalance scales a defense by quarters: two more for Def UP (Eth Def UP), two fewer for its
				// status 40h (the mental word's 200h, which its Deathblows give).
				if (bEther)
				{
					uint32 Scale = ((Target.EtherBuffs | Target.PermanentEtherBuffs) & 0x4000) != 0 ? 6 : 4;
					if ((Target.Mental & 0x200) != 0)
					{
						Scale -= 2;
					}
					EtherDefense = EtherDefense * Scale >> 2;
				}
				else if (!bTargetInGear)
				{
					const uint16 Buffs = Target.Buffs | Target.PermanentBuffs;
					uint32 Scale = (Buffs & 0x4000) != 0 ? 6 : 4;
					if ((Target.Status & 0x40) != 0)
					{
						Scale -= 2;
					}
					Defense = (Defense & 0xFFFF) * Scale >> 2;
					if ((Buffs & 0x100) != 0)
					{
						Defense = (Defense & 0xFFFF) * 3 >> 1;
					}
					if (Context.RuleOptions.bDefDownOnFoot && (Target.Status & DefDown) != 0)
					{
						Defense = (Defense & 0xFFFF) * 2 / 3;
					}
				}
			}
			else if (!bEther)
			{
				// Def UP's bit; the original tests one that nothing sets.
				const uint16 DefUp = Context.RuleOptions.bDefUpOnFoot ? 0x4000 : 0x100;
				if (!bTargetInGear && ((Target.Buffs | Target.PermanentBuffs) & DefUp) != 0)
				{
					Defense = (Defense & 0xFFFF) * 3 >> 1;
				}
				if (Context.RuleOptions.bDefDownOnFoot && !bTargetInGear && (Target.Status & DefDown) != 0)
				{
					Defense = (Defense & 0xFFFF) * 2 / 3;
				}
			}
			else if (((Target.EtherBuffs | Target.PermanentEtherBuffs) & 0x4000) != 0)
			{
				EtherDefense = EtherDefense * 3 >> 1;
			}
			uint32 Value = 0;
			switch (Command.DefenseFormula)
			{
			case 0: Value = Armor + Defense; break;
			case 1: Value = EtherDefense; break;
			case 2: Value = Armor; break;
			default: Context.Fault = EBattleFault::UnknownFormula; break;
			}
			if (!bTargetInGear)
			{
				uint32 Fallen = 0;
				for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
				{
					Fallen += (Context.Combatants[Slot].Status & 0x8000) != 0 ? 1 : 0;
				}
				if ((Target.EquipmentFlags & 4) != 0 && Fallen != 0 && !bEther)
				{
					Value = (Value & 0xFFFF) * (4 - Fallen) >> 2;
				}
				if ((Target.EquipmentFlags & 2) != 0 && Fallen != 0 && !bEther)
				{
					Value = (Value & 0xFFFF) * (Fallen + 2) >> 1;
				}
				if ((Target.EquipmentFlags & 1) != 0 && Fallen != 0)
				{
					Value = (Value & 0xFFFF) * (Fallen + 2) >> 1;
				}
			}
			return static_cast<int16>(Value);
		}

		void ApplyElement(FBattleContext& Context, uint16& Attack, uint16& Defense, uint8& Hit)
		{
			const FBattleCommand& Command = BattleRules::Command(Context);
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			const FBattleCombatant& Target = BattleRules::Target(Context);
			const uint16 ActorWord = InGear(Context, Context.Action.ActorSlot)
				? static_cast<uint16>(ActorGear(Context).Elements | ActorGear(Context).PermanentElements)
				: static_cast<uint16>(Actor.Elements | Actor.PermanentElements);
			const uint32 ActorElement = ActorWord >> 12;
			uint32 Element = Command.Element & 0x3F;
			if (Element == 0 && ActorElement != 0)
			{
				Element = ActorElement;
			}
			int32 Scale = 10;
			if ((Element & Target.Weakness & 0x3F) != 0)
			{
				Scale = (Target.Weakness & 0x40) != 0 ? 18 : 15;
				if ((Element & ActorElement) != 0)
				{
					Scale += 2;
				}
			}
			uint32 GuardWord = 0;
			bool bMaxer = false;
			if (InGear(Context, Context.Action.TargetSlot))
			{
				const FBattleGearPart& Gear = TargetGear(Context);
				GuardWord = Gear.Elements | Gear.PermanentElements;
				bMaxer = (GuardWord & 2) != 0;
			}
			else
			{
				GuardWord = Target.Elements | Target.PermanentElements;
				bMaxer = ((GuardWord >> 1) & 1) != 0;
			}
			const uint32 Guard = (GuardWord & 0xF00) >> 8;
			if ((Element & Guard) != 0)
			{
				auto Block = [&Scale, &Hit, GuardWord]()
				{
					Scale -= (GuardWord & 4) != 0 ? 6 : 3;
					if ((GuardWord & 8) != 0)
					{
						Hit = 4;
					}
				};
				if (IsEther(Command))
				{
					Block();
				}
				if (bMaxer)
				{
					Block();
				}
			}
			// Perfect Works' rebalance adds weak points without a block, in the word's 10h (fire), 20h (water), 40h (wind)
			// and 80h (earth).
			const uint32 WeakPoints = Context.bPerfectWorksRebalance ? Guard | ((GuardWord & 0xF0) >> 4) : Guard;
			uint32 Opposite = 0;
			switch (Element)
			{
			case 1: Opposite = WeakPoints & 2; break;
			case 2: Opposite = WeakPoints & 1; break;
			case 4: Opposite = WeakPoints & 8; break;
			case 8: Opposite = WeakPoints & 4; break;
			default: break;
			}
			if (Opposite != 0)
			{
				Scale += 3;
			}
			if (static_cast<int8>(Scale) < 1)
			{
				Scale = 1;
			}
			Attack = static_cast<uint16>(static_cast<int32>(Attack) * static_cast<int8>(Scale) / 10);
			// Perfect Works' rebalance scales an element's defense by tenths too: up for the target's ether buff 800h (a
			// gear's buff 100h), down for 400h (a gear's 4000h).
			if (Context.bPerfectWorksRebalance && Element != 0)
			{
				const bool bGear = InGear(Context, Context.Action.TargetSlot);
				const uint16 Buffs = bGear ? TargetGear(Context).Buffs : Target.EtherBuffs;
				const int32 DefenseScale = 10 + ((Buffs & (bGear ? 0x100 : 0x800)) != 0 ? 2 : 0) - ((Buffs & (bGear ? 0x4000 : 0x400)) != 0 ? 2 : 0);
				Defense = static_cast<uint16>(static_cast<int32>(Defense) * DefenseScale / 10);
			}
			if ((Actor.EquipmentFlags & 0x10) != 0)
			{
				Attack = AddFifth(Attack);
			}
			if ((Target.EquipmentFlags & 0x10) != 0)
			{
				Defense = AddFifth(Defense);
			}
		}

		void ChuChuPowerBonus(FBattleContext& Context, uint16& Power)
		{
			uint32 Pinches = 0;
			for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
			{
				const FBattleCombatant& Member = Context.Combatants[Slot];
				if (Member.Character != Fei && Member.Character != Bart)
				{
					continue;
				}
				const bool bGear = Member.IsInGear();
				const uint32 Max = bGear ? Member.GearPart.MaxHp : Member.MaxHp;
				const uint32 Hp = bGear ? Member.GearPart.Hp : Member.Hp;
				Pinches += Hp < (Max >> 1) ? 1 : 0;
				Pinches += Hp < (Max >> 2) ? 1 : 0;
			}
			if (Pinches != 0)
			{
				Power = static_cast<uint16>(Power + Pinches * (Power >> 1));
			}
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			// Perfect Works' rebalance: 25 and 100 for the original's 10 and 60.
			const bool bRebalance = Context.bPerfectWorksRebalance;
			const int32 Chance = (Actor.EquipmentFlags & 0x200) != 0 ? (bRebalance ? 100 : 60) : (bRebalance ? 25 : 10);
			const uint32 Scale = (Actor.EquipmentFlags & 0x400) != 0 ? 4 : 3;
			if (Percent(Context) < Chance)
			{
				Power = static_cast<uint16>(static_cast<int32>(Scale * Power) >> 1);
			}
		}

		void RollEtherFailure(FBattleContext& Context)
		{
			const FBattleCombatant& Actor = BattleRules::Actor(Context);
			const int32 Chance = static_cast<int32>(Actor.Ether) + BattleRules::Command(Context).EtherBonus;
			// Perfect Works' rebalance turns the failure into a critical hit that her ether makes likelier.
			const int32 Roll = Percent(Context);
			if (Context.bPerfectWorksRebalance ? Roll < Chance : Chance <= Roll)
			{
				Context.Action.Message = 0x38;
				Context.Action.bEtherFailed = true;
			}
		}

		void HandlerDamage(FBattleContext& Context)
		{
			FBattleAction& Action = Context.Action;
			const FBattleCommand& Command = BattleRules::Command(Context);
			FBattleCombatant& Actor = BattleRules::Actor(Context);
			FBattleCombatant& Target = BattleRules::Target(Context);
			const bool bEther = IsEther(Command);
			int32 Power = Command.Power;
			if ((Target.Traits & (bEther ? 0x4000 : 0x8000)) != 0)
			{
				Action.Results[Action.TargetSlot] = BattleResults::HpDamage;
				return;
			}
			uint8 Hit = RollHit(Context);
			uint16 Attack = static_cast<uint16>(CalcAttack(Context));
			uint16 Defense = static_cast<uint16>(CalcDefense(Context));
			const uint16 ActorEther = Actor.EtherBuffs | Actor.PermanentEtherBuffs;
			if ((ActorEther & 8) != 0) Attack = static_cast<uint16>(Attack + Attack / 5);
			if ((ActorEther & 2) != 0) Attack = static_cast<uint16>(Attack + Attack / 10);
			if ((ActorEther & 4) != 0) Attack = static_cast<uint16>(Attack - Attack / 5);
			if ((ActorEther & 1) != 0) Attack = static_cast<uint16>(Attack - Attack / 10);
			const uint16 TargetEther = Target.EtherBuffs | Target.PermanentEtherBuffs;
			// Perfect Works' rebalance moves the tenths of the target's to 200h and 100h.
			const bool bRebalance = Context.bPerfectWorksRebalance;
			if ((TargetEther & 4) != 0) Defense = static_cast<uint16>(Defense + Defense / 5);
			if ((TargetEther & (bRebalance ? 0x200 : 1)) != 0) Defense = static_cast<uint16>(Defense + Defense / 10);
			if ((TargetEther & 8) != 0) Defense = static_cast<uint16>(Defense - Defense / 5);
			if ((TargetEther & (bRebalance ? 0x100 : 2)) != 0) Defense = static_cast<uint16>(Defense - Defense / 10);
			if ((Command.Element & 0x10) != 0)
			{
				if ((Target.MentalImmunity & 0x40) == 0)
				{
					Target.Mental |= 0x40;
				}
				const uint16 ActorElements = Actor.Elements | Actor.PermanentElements;
				if ((ActorElements & 0x4000) != 0)
				{
					Action.Results[Action.ActorSlot] = BattleResults::EpHeal;
					Action.Amounts[Action.ActorSlot] = static_cast<uint32>(Actor.MaxEp / 10) << 1;
				}
				if ((ActorElements & 0x1000) != 0)
				{
					Action.Results[Action.ActorSlot] = BattleResults::HpHeal;
					Action.Amounts[Action.ActorSlot] = static_cast<uint32>(Actor.MaxHp / 10) << 1;
				}
			}
			if ((Command.Element & 0x20) != 0 && (Target.MentalImmunity & 0x80) == 0)
			{
				Target.Mental |= 0x80;
			}
			// Perfect Works' rebalance adds the mental words' 4000h and 8000h.
			if ((Target.Mental & (bRebalance ? 0x4040 : 0x40)) != 0)
			{
				Defense = static_cast<uint16>(Defense - (Defense >> 2));
				Target.Mental &= 0xFFBF;
			}
			if ((Actor.Mental & (bRebalance ? 0x8080 : 0x80)) != 0)
			{
				Attack = static_cast<uint16>(Attack - (Attack >> 2));
				Actor.Mental &= 0xFF7F;
			}
			if (bRebalance)
			{
				// Its Pierce leaves the defense out unless the actor's permanent elements have bit 1; either gives power
				// 20, and a Deathblow keeps its own.
				const bool bKeepsDefense = (Actor.PermanentElements & 1) != 0;
				const bool bPierce = (Command.Flags & BattleCommandFlags::PerfectWorksPierce) != 0;
				if (bPierce && !bKeepsDefense)
				{
					Defense = 0;
				}
				if (bPierce || bKeepsDefense)
				{
					Power = 20;
				}
			}
			else if ((Command.Flags & BattleCommandFlags::Deathblow) != 0)
			{
				Power = 20;
			}
			ApplyElement(Context, Attack, Defense, Hit);
			const int32 AttackScale = bEther ? 5 : 4;
			const int32 DefenseScale = bEther ? 4 : 3;
			int32 Damage = 0;
			if (bRebalance)
			{
				// Its base adds the power and takes a quarter of the attack off.
				Damage = AttackScale * Attack - (Attack >> 2) + Power;
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
			else if (Damage < 10)
			{
				Damage += Draw(Context) % 2;
			}
			else
			{
				Damage += Draw(Context) % (Damage / 10 + 2);
			}
			switch (Hit)
			{
			case 1:
				Damage = std::max(Damage, 1);
				Action.Results[Action.TargetSlot] = BattleResults::HpDamage;
				break;
			case 2:
				Damage /= 2;
				Action.Results[Action.TargetSlot] = BattleResults::HalvedDamage;
				break;
			case 3:
				Damage = 0;
				Action.Results[Action.TargetSlot] = BattleResults::Miss;
				break;
			case 4:
				Action.Results[Action.TargetSlot] = BattleResults::HpHeal;
				break;
			case 5:
				Action.Results[Action.TargetSlot] = BattleResults::Counterattack;
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
