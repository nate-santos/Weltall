#include "Battle/BattleTables.h"

#include "Formats/SectionContainer.h"
#include "Media/GameFileIndex.h"

#include <optional>
#include <string>
#include <utility>

namespace Weltall
{
	namespace
	{
		constexpr int32 BattleDataDirectory = 12;
		constexpr int32 BattleDataFileNumber = 3;
		constexpr uint32 ItemsSection = 3;
		constexpr uint32 EnemyCommandsSection = 4;
		constexpr uint32 FirstCharacterSection = 5;
		constexpr uint32 FirstGearSection = 17;
		/** The weapons' table (16 bytes a weapon, the attack at +0Ch), whose records from the first ammunition the battle copies. */
		constexpr uint32 WeaponsSection = 37;
		constexpr std::size_t WeaponRecordSize = 0x10;
		constexpr std::size_t WeaponAttack = 0x0C;

		/** Reads Count records of a section, each made by FromRecord; an error if the section is shorter. */
		template <typename TRecord, std::size_t Count>
		std::optional<FError> ReadRecords(std::span<const uint8> File, uint32 Section, std::array<TRecord, Count>& Records)
		{
			const TResult<std::vector<uint8>> Unpacked = UnpackContainerSection(File, Section);
			if (!Unpacked)
			{
				return Unpacked.Error();
			}
			if (Unpacked->size() < Count * TRecord::RecordSize)
			{
				return MakeError("section " + std::to_string(Section) + " of the battle's data holds fewer than " + std::to_string(Count) + " records");
			}
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				Records[Index] = TRecord::FromRecord(std::span<const uint8>(*Unpacked).subspan(Index * TRecord::RecordSize).template first<TRecord::RecordSize>());
			}
			return std::nullopt;
		}
	}

	TResult<FBattleTables> FBattleTables::Parse(std::span<const uint8> BattleDataFile)
	{
		FBattleTables Tables;
		if (std::optional<FError> Error = ReadRecords(BattleDataFile, ItemsSection, Tables.BattleItems))
		{
			return std::move(*Error);
		}
		if (std::optional<FError> Error = ReadRecords(BattleDataFile, EnemyCommandsSection, Tables.Enemies))
		{
			return std::move(*Error);
		}
		Tables.Characters.resize(CharacterCount);
		for (int32 Character = 0; Character < CharacterCount; ++Character)
		{
			if (std::optional<FError> Error = ReadRecords(BattleDataFile, FirstCharacterSection + static_cast<uint32>(Character), Tables.Characters[static_cast<std::size_t>(Character)]))
			{
				return std::move(*Error);
			}
		}
		Tables.Gears.resize(GearCount);
		for (int32 Gear = 0; Gear < GearCount; ++Gear)
		{
			if (std::optional<FError> Error = ReadRecords(BattleDataFile, FirstGearSection + static_cast<uint32>(Gear), Tables.Gears[static_cast<std::size_t>(Gear)]))
			{
				return std::move(*Error);
			}
		}
		const TResult<std::vector<uint8>> Weapons = UnpackContainerSection(BattleDataFile, WeaponsSection);
		if (!Weapons)
		{
			return Weapons.Error();
		}
		if (Weapons->size() < (FirstAmmunition + AmmunitionCount) * WeaponRecordSize)
		{
			return MakeError("the battle's weapons table ends before the weapons it copies");
		}
		for (std::size_t Index = 0; Index < Tables.Ammunition.size(); ++Index)
		{
			Tables.Ammunition[Index] = (*Weapons)[(FirstAmmunition + Index) * WeaponRecordSize + WeaponAttack];
		}
		return Tables;
	}

	TResult<FBattleTables> FBattleTables::Load(const FCdImage& Image, const FGameFileIndex& Index)
	{
		const FIndexRecord* Record = Index.Find(BattleDataDirectory, BattleDataFileNumber);
		if (Record == nullptr)
		{
			return MakeError("the disc lacks the battle's data file");
		}
		const TResult<std::vector<uint8>> File = ReadFileSectors(Image, *Record);
		if (!File)
		{
			return File.Error();
		}
		return Parse(*File);
	}
}
