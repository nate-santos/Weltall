#include "Game/CharacterEquipment.h"

#include "Formats/SectionContainer.h"
#include "Foundation/LittleEndian.h"
#include "Game/GameState.h"
#include "Game/GameStateLayout.h"
#include "Media/GameFileIndex.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr uint32 WeaponsSection = 2;
		constexpr uint32 AccessoriesSection = 3;
		constexpr int32 GameDataDirectory = 16;
		constexpr int32 GameDataFile = 2;

		/** The character record's fields the equipment sets. */
		namespace Field
		{
			constexpr std::size_t WeaponEffectFlags = 0x00;
			constexpr std::size_t WeaponEffectChance = 0x02;
			constexpr std::size_t WeaponEffectKind = 0x03;
			constexpr std::size_t WeaponAttack = 0x04;
			/** Billy's second gun: its flags, chance, kind and attack. */
			constexpr std::size_t SecondGunFlags = 0x18;
			constexpr std::size_t SecondGunChance = 0x1A;
			constexpr std::size_t SecondGunKind = 0x1B;
			constexpr std::size_t SecondGunAttack = 0x1C;
			constexpr std::size_t AttackBonus = 0x28;
			constexpr std::size_t DefenseBonus = 0x29;
			constexpr std::size_t AgilityBonus = 0x2A;
			constexpr std::size_t EtherBonus = 0x2B;
			constexpr std::size_t EtherDefenseBonus = 0x2C;
			constexpr std::size_t ArmorDefense = 0x2D;
			constexpr std::size_t HitBonus = 0x2E;
			constexpr std::size_t EvadeBonus = 0x2F;
			constexpr std::size_t HpBonus = 0x30;
			constexpr std::size_t EpBonus = 0x31;
			constexpr std::size_t EquipmentFlags = 0x32;
			constexpr std::size_t Id = 0x56;
			constexpr std::size_t Attack = 0x58;
			constexpr std::size_t Defense = 0x59;
			constexpr std::size_t Agility = 0x5A;
			constexpr std::size_t Ether = 0x5B;
			constexpr std::size_t EtherDefense = 0x5C;
			constexpr std::size_t Hit = 0x5E;
			constexpr std::size_t Evade = 0x5F;
			constexpr std::size_t Weapon = 0x6A;
			/** Billy's guns: the first (his weapon) and the second. */
			constexpr std::size_t FirstGun = 0x6F;
			constexpr std::size_t SecondGun = 0x72;
			constexpr std::size_t Accessories = 0x74;
			constexpr std::array<std::size_t, 5> SpecialFlags = {0x7E, 0x82, 0x86, 0x8A, 0x8E};
			constexpr std::size_t ElementFlags = 0x8E;
			constexpr std::size_t LearningBonus = 0xA1;
		}

		/** An accessory record's fields. */
		namespace AccessoryField
		{
			constexpr std::size_t ArmorDefense = 0x08;
			constexpr std::size_t Special = 0x09;
			constexpr std::size_t SpecialValue = 0x0A;
			/** The bonus: its value in the low byte, the stats it adds to in the high byte's bits. */
			constexpr std::size_t Bonus = 0x0C;
		}

		/** A weapon record's fields. */
		namespace WeaponField
		{
			constexpr std::size_t EffectFlags = 0x08;
			constexpr std::size_t EffectChance = 0x0A;
			constexpr std::size_t EffectKind = 0x0B;
			constexpr std::size_t Attack = 0x0C;
		}

		constexpr uint8 Billy = 4;
		/** A weapon of this effect kind adds its effect's element bits to the record's. */
		constexpr uint8 ElementalWeapon = 100;
		constexpr std::size_t AccessorySlots = 3;

		/** A bonus mask bit and the record field it adds to. */
		struct FBonusField
		{
			uint16 Mask;
			std::size_t Offset;
		};

		/** The bonus mask bits, highest first. */
		constexpr std::array<FBonusField, 8> BonusFields = {{
			{0x8000, Field::AttackBonus},
			{0x4000, Field::DefenseBonus},
			{0x2000, Field::AgilityBonus},
			{0x1000, Field::EtherBonus},
			{0x0800, Field::EtherDefenseBonus},
			{0x0400, Field::HitBonus},
			{0x0200, Field::EvadeBonus},
			{0x0100, Field::ArmorDefense},
		}};

		constexpr uint16 AttackCap = 250;
		constexpr uint16 PercentCap = 99;
		/** Agility over this shows as the battle's cap. */
		constexpr uint16 OriginalAgilityLimit = 20;
		constexpr uint16 AgilityCap = 16;

		std::span<const uint8> TableRecord(const std::vector<uint8>& Table, uint8 Id)
		{
			const std::size_t Offset = static_cast<std::size_t>(Id) * FEquipmentTables::RecordSize;
			if (Offset + FEquipmentTables::RecordSize > Table.size())
			{
				return {};
			}
			return std::span<const uint8>(Table).subspan(Offset, FEquipmentTables::RecordSize);
		}

		uint8 ByteOf(std::span<const uint8> Record, std::size_t Offset)
		{
			return Record.empty() ? 0 : Record[Offset];
		}

		uint16 WordOf(std::span<const uint8> Record, std::size_t Offset)
		{
			return Record.empty() ? 0 : LittleEndian::ReadU16(Record, Offset);
		}

		void AddByte(std::span<uint8> Character, std::size_t Offset, uint8 Value)
		{
			Character[Offset] = static_cast<uint8>(Character[Offset] + Value);
		}

		void OrWord(std::span<uint8> Character, std::size_t Offset, uint16 Value)
		{
			LittleEndian::WriteU16(Character, Offset, static_cast<uint16>(LittleEndian::ReadU16(Character, Offset) | Value));
		}

		/** A gun or weapon's attack and effect into the record's fields. */
		void ApplyWeapon(std::span<uint8> Character, std::span<const uint8> Weapon, std::size_t Flags, std::size_t Chance, std::size_t Kind, std::size_t Attack)
		{
			Character[Attack] = ByteOf(Weapon, WeaponField::Attack);
			LittleEndian::WriteU16(Character, Flags, WordOf(Weapon, WeaponField::EffectFlags));
			Character[Chance] = ByteOf(Weapon, WeaponField::EffectChance);
			Character[Kind] = ByteOf(Weapon, WeaponField::EffectKind);
		}
	}

	FEquipmentTables FEquipmentTables::Make(std::vector<uint8> InWeapons, std::vector<uint8> InAccessories)
	{
		FEquipmentTables Tables;
		Tables.Weapons = std::move(InWeapons);
		Tables.Accessories = std::move(InAccessories);
		return Tables;
	}

	TResult<FEquipmentTables> FEquipmentTables::Parse(std::span<const uint8> GameData)
	{
		TResult<std::vector<uint8>> Weapons = UnpackContainerSection(GameData, WeaponsSection);
		if (!Weapons)
		{
			return MakeError("the weapons cannot be read: " + Weapons.Error().Message);
		}
		TResult<std::vector<uint8>> Accessories = UnpackContainerSection(GameData, AccessoriesSection);
		if (!Accessories)
		{
			return MakeError("the accessories cannot be read: " + Accessories.Error().Message);
		}
		return Make(std::move(*Weapons), std::move(*Accessories));
	}

	TResult<FEquipmentTables> FEquipmentTables::Load(const FCdImage& Image, const FGameFileIndex& Index)
	{
		const FIndexRecord* Record = Index.Find(GameDataDirectory, GameDataFile);
		if (Record == nullptr)
		{
			return MakeError("the disc lacks the game's data file (16/2)");
		}
		TResult<std::vector<uint8>> File = ReadFileSectors(Image, *Record);
		if (!File)
		{
			return File.Error();
		}
		return Parse(*File);
	}

	std::span<const uint8> FEquipmentTables::Weapon(uint8 Id) const
	{
		return TableRecord(Weapons, Id);
	}

	std::span<const uint8> FEquipmentTables::Accessory(uint8 Id) const
	{
		return TableRecord(Accessories, Id);
	}

	void RecomputeCharacterEquipment(FGameState& State, int32 Character, const FEquipmentTables& Tables)
	{
		const std::span<uint8> Record = State.Block().subspan(GameStateLayout::CharacterOffset(static_cast<std::size_t>(Character)), GameStateLayout::CharacterSize);
		for (const std::size_t Offset : {Field::ArmorDefense, Field::AttackBonus, Field::DefenseBonus, Field::AgilityBonus, Field::EtherBonus,
				 Field::EtherDefenseBonus, Field::HitBonus, Field::EvadeBonus, Field::HpBonus, Field::EpBonus, Field::LearningBonus})
		{
			Record[Offset] = 0;
		}
		LittleEndian::WriteU16(Record, Field::EquipmentFlags, 0);
		for (const std::size_t Offset : Field::SpecialFlags)
		{
			LittleEndian::WriteU16(Record, Offset, 0);
		}
		for (std::size_t Slot = 0; Slot < AccessorySlots; ++Slot)
		{
			const std::span<const uint8> Accessory = Tables.Accessory(Record[Field::Accessories + Slot]);
			AddByte(Record, Field::ArmorDefense, ByteOf(Accessory, AccessoryField::ArmorDefense));
			const uint16 SpecialValue = WordOf(Accessory, AccessoryField::SpecialValue);
			switch (ByteOf(Accessory, AccessoryField::Special))
			{
			case 1:
			case 2:
			case 3:
			case 4:
				OrWord(Record, Field::SpecialFlags[static_cast<std::size_t>(ByteOf(Accessory, AccessoryField::Special) - 1)], SpecialValue);
				break;
			case 5:
				OrWord(Record, Field::EquipmentFlags, SpecialValue);
				break;
			case 7:
				OrWord(Record, Field::ElementFlags, SpecialValue);
				break;
			case 8:
			case 9:
				AddByte(Record, Field::HpBonus, static_cast<uint8>(SpecialValue));
				break;
			case 10:
				AddByte(Record, Field::LearningBonus, static_cast<uint8>(SpecialValue));
				break;
			default:
				break;
			}
			const uint16 Bonus = WordOf(Accessory, AccessoryField::Bonus);
			for (const auto& [Mask, Offset] : BonusFields)
			{
				if ((Bonus & Mask) != 0)
				{
					AddByte(Record, Offset, static_cast<uint8>(Bonus));
				}
			}
		}
		ApplyWeapon(Record, Tables.Weapon(Record[Field::Weapon]), Field::WeaponEffectFlags, Field::WeaponEffectChance, Field::WeaponEffectKind, Field::WeaponAttack);
		if (Record[Field::Id] == Billy)
		{
			ApplyWeapon(Record, Tables.Weapon(Record[Field::FirstGun]), Field::WeaponEffectFlags, Field::WeaponEffectChance, Field::WeaponEffectKind,
				Field::WeaponAttack);
			ApplyWeapon(Record, Tables.Weapon(Record[Field::SecondGun]), Field::SecondGunFlags, Field::SecondGunChance, Field::SecondGunKind,
				Field::SecondGunAttack);
		}
		if (Record[Field::WeaponEffectKind] == ElementalWeapon)
		{
			OrWord(Record, Field::ElementFlags, LittleEndian::ReadU16(Record, Field::WeaponEffectFlags));
		}
	}

	FCharacterStats ComputeCharacterStats(const FGameState& State, int32 Character, bool bOriginalAgilityCap, bool bPerfectWorksRules)
	{
		const std::span<const uint8> Record =
			State.Block().subspan(GameStateLayout::CharacterOffset(static_cast<std::size_t>(Character)), GameStateLayout::CharacterSize);
		const auto Sum = [&Record](std::initializer_list<std::size_t> Offsets)
		{
			uint32 Total = 0;
			for (const std::size_t Offset : Offsets)
			{
				Total += Record[Offset];
			}
			return static_cast<uint16>(Total);
		};
		FCharacterStats Stats;
		std::array<uint16, FCharacterStats::Count>& Values = Stats.Values;
		if (Record[Field::Id] == Billy)
		{
			Values[FCharacterStats::Attack] = static_cast<uint16>(Sum({Field::WeaponAttack, Field::SecondGunAttack}) * 6u / 10u);
		}
		else
		{
			Values[FCharacterStats::Attack] = Sum({Field::WeaponAttack, Field::Attack, Field::AttackBonus});
		}
		Values[FCharacterStats::Hit] = Sum({Field::Hit, Field::HitBonus});
		Values[FCharacterStats::Defense] = Sum({Field::ArmorDefense, Field::Defense, Field::DefenseBonus});
		Values[FCharacterStats::Evade] = Sum({Field::Evade, Field::EvadeBonus});
		Values[FCharacterStats::Ether] = Sum({Field::Ether, Field::EtherBonus});
		Values[FCharacterStats::EtherDefense] = Sum({Field::EtherDefense, Field::EtherDefenseBonus});
		Values[FCharacterStats::Agility] = Sum({Field::Agility, Field::AgilityBonus});
		for (const FCharacterStats::EStat Stat : {FCharacterStats::Attack, FCharacterStats::Defense, FCharacterStats::Ether, FCharacterStats::EtherDefense})
		{
			Values[Stat] = std::min(Values[Stat], AttackCap);
		}
		Values[FCharacterStats::Hit] = std::min(Values[FCharacterStats::Hit], PercentCap);
		Values[FCharacterStats::Evade] = std::min(Values[FCharacterStats::Evade], PercentCap);
		uint16& Agility = Values[FCharacterStats::Agility];
		if (bOriginalAgilityCap)
		{
			// Perfect Works' rebalance's menu shows at most 20 where the original's turns a value over 20 into 16.
			Agility = bPerfectWorksRules ? std::min(Agility, OriginalAgilityLimit) : Agility > OriginalAgilityLimit ? AgilityCap : Agility;
		}
		else
		{
			Agility = std::min(Agility, AgilityCap);
		}
		return Stats;
	}
}
