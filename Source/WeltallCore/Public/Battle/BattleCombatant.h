#pragma once

#include "Foundation/Types.h"

#include <array>
#include <cstddef>
#include <span>

namespace Weltall
{
	/** A character's weapon slot: a weapon, or for Billy one of his guns and the ammunition loaded. */
	struct FBattleWeaponSlot
	{
		/** The status the weapon inflicts on a hit, its chance and its group. */
		uint16 StatusBits = 0;
		uint8 StatusChance = 0;
		uint8 StatusGroup = 0;
		/** The weapon's attack; for Billy's ether ammunition, its element. */
		uint8 Attack = 0;
	};

	/** A gear's weapon slot. */
	struct FBattleGearWeaponSlot
	{
		uint16 StatusBits = 0;
		uint8 Attack = 0;
		int8 StatusChance = 0;
		uint8 StatusKind = 0;
		/** Billy's gears: the ammunition loaded. */
		uint8 Ammunition = 0;
	};

	/** The gear a combatant fights in: a copy of the gear's record, or a gear-sized enemy's own. */
	struct FBattleGearPart
	{
		static constexpr std::size_t RecordSize = 0xA4;

		uint8 Frame = 0;
		uint8 Engine = 0;
		/** Billy's gears: where the counts of the ammunition of weapon slots 0 and 2 are kept. */
		uint8 AmmunitionIndex0 = 0;
		uint8 AmmunitionIndex2 = 0;
		std::array<FBattleGearWeaponSlot, 3> Weapons{};
		uint16 Fuel = 0;
		uint16 MaxFuel = 0;
		uint8 Output = 0;
		uint8 Byte3D = 0;
		uint8 Byte3E = 0;
		/** The output's multiplier: the gear's own and the parts' bonus, set at the battle's start. */
		uint8 AttackMultiplier = 0;
		/** What the parts add to the gear's defense, ether defense, weight, guard, hit, agility and ether amplifier. */
		uint16 PartsDefense = 0;
		uint16 PartsEtherDefense = 0;
		int16 PartsWeight = 0;
		uint16 Word46 = 0;
		uint8 OverweightPenalty = 0;
		uint8 PartsGuard = 0;
		uint8 PartsHit = 0;
		uint8 PartsAgility = 0;
		/** The Repair parts' level: 1, 3, 5, 7 or 9 tenths of the maximum HP. */
		uint8 RepairLevel = 0;
		/** The parts' agility on each kind of ground a stage can have. */
		std::array<uint8, 4> TerrainAgility{};
		uint8 PartsEtherAmplifier = 0;
		uint8 MultiplierBonus = 0;
		uint8 ChargeMultiplier = 0;
		/**
		 * The variant the gear's model is read with, then bytes the model's effect events add to the effects
		 * they name: the show's alone, so the rules' layout leaves them out and FromRecord reads them itself.
		 */
		std::array<uint8, 4> ModelBytes{};
		static constexpr std::size_t ModelBytesOffset = 0x5C;
		uint32 Hp = 0;
		uint32 MaxHp = 0;
		uint16 WeightCapacity = 0;
		uint16 Word6C = 0;
		uint16 ResistanceMask = 0;
		uint16 Defense = 0;
		uint16 EtherDefense = 0;
		/** The gear's own attack multiplier. */
		uint8 BaseMultiplier = 0;
		uint8 WeightTolerance = 0;
		uint16 Status = 0;
		uint16 StatusImmunity = 0;
		uint16 Buffs = 0;
		uint16 PermanentBuffs = 0;
		/** Bits 12-15 the attack element, 8-11 the guard element. */
		uint16 Elements = 0;
		uint16 PermanentElements = 0;
		/** Damage taken off per kind of damage, in twentieths. */
		std::array<uint8, 16> Resistances{};
		uint8 Agility = 0;
		/** Percent of the defense a broken armor loses. */
		uint8 ArmorBreak = 0;
		uint8 Guard = 0;
		uint8 Byte9D = 0;
		uint8 EtherAmplifier = 0;
		int8 HitBonus = 0;
	};

	/**
	 * One of a battle's eleven combatants (party slots 0-2, enemy slots 3-10): the character record
	 * it was copied from, the gear it may fight in, and what the battle keeps beside them. An
	 * enemy's is its scene's record whole.
	 */
	struct FBattleCombatant
	{
		static constexpr std::size_t RecordSize = 0x170;
		static constexpr std::size_t CharacterRecordSize = 0xA4;
		static constexpr std::size_t EtherAmmunitionOffset = 0x70;
		static constexpr std::size_t ExtraBonusesOffset = 0x20;
		static constexpr int32 DurationCount = 14;

		std::array<FBattleWeaponSlot, 4> Weapons{};
		/**
		 * Perfect Works' rebalance: what its equipment and items add besides to attack, defense, ether, ether defense and
		 * agility (record 20h-24h). Only its rules read them, so they lie outside the layout of the original's.
		 */
		std::array<uint8, 5> ExtraBonuses{};
		/** What the equipment adds to attack, defense, agility, ether, ether defense, armor, hit and evade. */
		std::array<uint8, 8> EquipmentBonuses{};
		/** Max HP and max EP added at the battle's start, in twentieths. */
		uint8 HpBonus = 0;
		uint8 EpBonus = 0;
		uint16 EquipmentFlags = 0;
		/** Immunities and behaviours of enemies: to physical or ether damage, to some commands, acting first... */
		uint16 Traits = 0;
		/** Enemies: heals and damage reversed, ether absorbed. */
		uint16 Absorption = 0;
		/** Element weakness: bits 0-5, and 0x40 weaker still. */
		uint16 Weakness = 0;
		/** Hits that dealt exactly the target's remaining HP. */
		uint16 ExactKills = 0;
		uint32 Exp = 0;
		uint32 EtherExp = 0;
		uint32 ExpToNext = 0;
		uint32 EtherExpToNext = 0;
		uint16 Hp = 0;
		uint16 MaxHp = 0;
		uint16 Ep = 0;
		uint16 MaxEp = 0;
		/** Gears: the Deathblows used, which make Infinity likelier. */
		uint8 InfinityCounter = 0;
		/** Added to a Deathblow counter per basic attack. */
		uint8 LearningRate = 0;
		uint8 Character = 0;
		uint8 Byte57 = 0;
		uint8 Attack = 0;
		uint8 Defense = 0;
		uint8 Agility = 0;
		uint8 Ether = 0;
		uint8 EtherDefense = 0;
		uint8 Byte5D = 0;
		uint8 Hit = 0;
		uint8 Evade = 0;
		/** Enemies: the chance of their drains and fixed damage that name none. */
		uint8 StatusRate = 0;
		uint8 Byte61 = 0;
		uint8 Level = 0;
		uint8 EtherLevel = 0;
		std::array<uint8, 4> Bytes64{};
		/** Billy's guns: the ammunition each fires. */
		uint8 PistolAmmunition = 0;
		uint8 BigGunAmmunition = 0;
		/**
		 * Billy's ether guns, right and left: the ether ammunition each holds (record 70h, 71h). Only the ether guns' fix
		 * reads them, so they lie outside the layout of the original's rules.
		 */
		std::array<uint8, 2> EtherAmmunition{};
		/** The party's commands switched off: bit 0x8000 >> i for command i of the menu. */
		uint16 DisabledCommands = 0;
		/** KO, Stop, Slow, Poison, ACC and Atk DOWN, Play Dead... */
		uint16 Status = 0;
		uint16 StatusImmunity = 0;
		/** Confusion, Sleep, Can't use Ether, the marks of light and dark... */
		uint16 Mental = 0;
		uint16 MentalImmunity = 0;
		/** The physical buffs, then those the equipment gives for good. */
		uint16 Buffs = 0;
		uint16 PermanentBuffs = 0;
		uint16 EtherBuffs = 0;
		uint16 PermanentEtherBuffs = 0;
		/** Bits 12-15 the attack element, 8-11 the guard element. */
		uint16 Elements = 0;
		uint16 PermanentElements = 0;
		/** Each basic attack's count toward the Deathblows it teaches. */
		std::array<uint16, 7> DeathblowCounters{};
		uint8 Gear = 0;
		uint8 LearningBonus = 0;

		FBattleGearPart GearPart;

		uint8 AttackLevel = 0;
		uint8 MaxAttackLevel = 0;
		/** Enemies: what defeating them gives. */
		uint32 ExpReward = 0;
		std::array<uint8, 2> DropChances{};
		std::array<uint8, 2> Drops{};
		std::array<uint8, 2> DropKinds{};
		uint16 GoldReward = 0;
		/** How the battle's EXP is shared out. */
		uint8 ExpShare = 0;
		uint8 EtherExpShare = 0;
		/** BattleFlags bits. */
		uint8 BattleFlags = 0;
		/**
		 * Turns (steps for Stop) each timed status has left: Stop, Slow, Sleep, Can't use Ether, the
		 * five physical buffs, three ether buffs, the attack and the guard element. A gear keeps its
		 * own statuses' here too, and its Infinity's turns share the Atk UP counter.
		 */
		std::array<uint8, DurationCount> Durations{};

		/** From a record as the game lays it out: an enemy's in its scene, or a character and a gear record joined. */
		WELTALLCORE_API static FBattleCombatant FromRecord(std::span<const uint8, RecordSize> Record);

		/** The status word at Offset of the record (the disabled commands at 7Ah to the permanent elements at 8Eh). */
		WELTALLCORE_API uint16& StatusWord(std::size_t Offset);

		bool IsInGear() const { return (BattleFlags & 0x80) != 0; }
	};

	namespace BattleFlags
	{
		inline constexpr uint8 Defending = 0x01;
		inline constexpr uint8 InGear = 0x80;
	}

	/**
	 * Calls Visit(offset, member) for each member of a combatant at its place in the game's record;
	 * the bytes left out are read by nothing in the battle.
	 */
	template <typename TCombatant, typename FVisit>
	void VisitBattleCombatantLayout(TCombatant& C, FVisit&& Visit)
	{
		for (std::size_t Slot = 0; Slot < C.Weapons.size(); ++Slot)
		{
			Visit(Slot * 8 + 0, C.Weapons[Slot].StatusBits);
			Visit(Slot * 8 + 2, C.Weapons[Slot].StatusChance);
			Visit(Slot * 8 + 3, C.Weapons[Slot].StatusGroup);
			Visit(Slot * 8 + 4, C.Weapons[Slot].Attack);
		}
		for (std::size_t Index = 0; Index < C.EquipmentBonuses.size(); ++Index)
		{
			Visit(0x28 + Index, C.EquipmentBonuses[Index]);
		}
		Visit(0x30, C.HpBonus);
		Visit(0x31, C.EpBonus);
		Visit(0x32, C.EquipmentFlags);
		Visit(0x34, C.Traits);
		Visit(0x36, C.Absorption);
		Visit(0x38, C.Weakness);
		Visit(0x3A, C.ExactKills);
		Visit(0x3C, C.Exp);
		Visit(0x40, C.EtherExp);
		Visit(0x44, C.ExpToNext);
		Visit(0x48, C.EtherExpToNext);
		Visit(0x4C, C.Hp);
		Visit(0x4E, C.MaxHp);
		Visit(0x50, C.Ep);
		Visit(0x52, C.MaxEp);
		Visit(0x54, C.InfinityCounter);
		Visit(0x55, C.LearningRate);
		Visit(0x56, C.Character);
		Visit(0x57, C.Byte57);
		Visit(0x58, C.Attack);
		Visit(0x59, C.Defense);
		Visit(0x5A, C.Agility);
		Visit(0x5B, C.Ether);
		Visit(0x5C, C.EtherDefense);
		Visit(0x5D, C.Byte5D);
		Visit(0x5E, C.Hit);
		Visit(0x5F, C.Evade);
		Visit(0x60, C.StatusRate);
		Visit(0x61, C.Byte61);
		Visit(0x62, C.Level);
		Visit(0x63, C.EtherLevel);
		for (std::size_t Index = 0; Index < C.Bytes64.size(); ++Index)
		{
			Visit(0x64 + Index, C.Bytes64[Index]);
		}
		Visit(0x6F, C.PistolAmmunition);
		Visit(0x72, C.BigGunAmmunition);
		Visit(0x7A, C.DisabledCommands);
		Visit(0x7C, C.Status);
		Visit(0x7E, C.StatusImmunity);
		Visit(0x80, C.Mental);
		Visit(0x82, C.MentalImmunity);
		Visit(0x84, C.Buffs);
		Visit(0x86, C.PermanentBuffs);
		Visit(0x88, C.EtherBuffs);
		Visit(0x8A, C.PermanentEtherBuffs);
		Visit(0x8C, C.Elements);
		Visit(0x8E, C.PermanentElements);
		for (std::size_t Index = 0; Index < C.DeathblowCounters.size(); ++Index)
		{
			Visit(0x90 + 2 * Index, C.DeathblowCounters[Index]);
		}
		Visit(0xA0, C.Gear);
		Visit(0xA1, C.LearningBonus);

		constexpr std::size_t G = 0xA4;
		auto& Part = C.GearPart;
		Visit(G + 0x02, Part.Frame);
		Visit(G + 0x03, Part.Engine);
		Visit(G + 0x04, Part.AmmunitionIndex0);
		Visit(G + 0x07, Part.AmmunitionIndex2);
		for (std::size_t Slot = 0; Slot < Part.Weapons.size(); ++Slot)
		{
			Visit(G + 0x10 + Slot * 8, Part.Weapons[Slot].StatusBits);
			Visit(G + 0x12 + Slot * 8, Part.Weapons[Slot].Attack);
			Visit(G + 0x13 + Slot * 8, Part.Weapons[Slot].StatusChance);
			Visit(G + 0x14 + Slot * 8, Part.Weapons[Slot].StatusKind);
			Visit(G + 0x15 + Slot * 8, Part.Weapons[Slot].Ammunition);
		}
		Visit(G + 0x38, Part.Fuel);
		Visit(G + 0x3A, Part.MaxFuel);
		Visit(G + 0x3C, Part.Output);
		Visit(G + 0x3D, Part.Byte3D);
		Visit(G + 0x3E, Part.Byte3E);
		Visit(G + 0x3F, Part.AttackMultiplier);
		Visit(G + 0x40, Part.PartsDefense);
		Visit(G + 0x42, Part.PartsEtherDefense);
		Visit(G + 0x44, Part.PartsWeight);
		Visit(G + 0x46, Part.Word46);
		Visit(G + 0x4A, Part.OverweightPenalty);
		Visit(G + 0x4C, Part.PartsGuard);
		Visit(G + 0x4D, Part.PartsHit);
		Visit(G + 0x4E, Part.PartsAgility);
		Visit(G + 0x4F, Part.RepairLevel);
		for (std::size_t Index = 0; Index < Part.TerrainAgility.size(); ++Index)
		{
			Visit(G + 0x50 + Index, Part.TerrainAgility[Index]);
		}
		Visit(G + 0x54, Part.PartsEtherAmplifier);
		Visit(G + 0x56, Part.MultiplierBonus);
		Visit(G + 0x57, Part.ChargeMultiplier);
		Visit(G + 0x60, Part.Hp);
		Visit(G + 0x64, Part.MaxHp);
		Visit(G + 0x68, Part.WeightCapacity);
		Visit(G + 0x6C, Part.Word6C);
		Visit(G + 0x6E, Part.ResistanceMask);
		Visit(G + 0x70, Part.Defense);
		Visit(G + 0x72, Part.EtherDefense);
		Visit(G + 0x74, Part.BaseMultiplier);
		Visit(G + 0x75, Part.WeightTolerance);
		Visit(G + 0x7C, Part.Status);
		Visit(G + 0x7E, Part.StatusImmunity);
		Visit(G + 0x80, Part.Buffs);
		Visit(G + 0x82, Part.PermanentBuffs);
		Visit(G + 0x84, Part.Elements);
		Visit(G + 0x86, Part.PermanentElements);
		for (std::size_t Index = 0; Index < Part.Resistances.size(); ++Index)
		{
			Visit(G + 0x88 + Index, Part.Resistances[Index]);
		}
		Visit(G + 0x98, Part.Agility);
		Visit(G + 0x99, Part.ArmorBreak);
		Visit(G + 0x9C, Part.Guard);
		Visit(G + 0x9D, Part.Byte9D);
		Visit(G + 0x9E, Part.EtherAmplifier);
		Visit(G + 0x9F, Part.HitBonus);

		Visit(0x148, C.AttackLevel);
		Visit(0x149, C.MaxAttackLevel);
		Visit(0x14C, C.ExpReward);
		Visit(0x150, C.DropChances[0]);
		Visit(0x151, C.DropChances[1]);
		Visit(0x152, C.Drops[0]);
		Visit(0x153, C.Drops[1]);
		Visit(0x154, C.DropKinds[0]);
		Visit(0x155, C.DropKinds[1]);
		Visit(0x156, C.GoldReward);
		Visit(0x158, C.ExpShare);
		Visit(0x159, C.EtherExpShare);
		Visit(0x15A, C.BattleFlags);
		for (std::size_t Index = 0; Index < C.Durations.size(); ++Index)
		{
			Visit(0x15C + Index, C.Durations[Index]);
		}
	}
}
