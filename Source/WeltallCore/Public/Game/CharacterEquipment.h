#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <array>
#include <span>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;
	class FGameState;

	/** The weapons' and the accessories' records, as the Menu reads them from the game's data file. */
	class FEquipmentTables
	{
	public:
		static constexpr std::size_t RecordSize = 0x10;

		/** From the tables as the game unpacks them. */
		WELTALLCORE_API static FEquipmentTables Make(std::vector<uint8> Weapons, std::vector<uint8> Accessories);

		/** From the game's data file, whose LZSS sections hold the tables. */
		WELTALLCORE_API static TResult<FEquipmentTables> Parse(std::span<const uint8> GameDataFile);

		/** Read from the disc as the Menu reads them. */
		WELTALLCORE_API static TResult<FEquipmentTables> Load(const FCdImage& Image, const FGameFileIndex& Index);

		/** A record by id; empty when the table ends before it. */
		WELTALLCORE_API std::span<const uint8> Weapon(uint8 Id) const;
		WELTALLCORE_API std::span<const uint8> Accessory(uint8 Id) const;

	private:
		std::vector<uint8> Weapons;
		std::vector<uint8> Accessories;
	};

	/**
	 * A character's equipment applied to its record as the Menu applies it after any change: the
	 * accessories' armor defense, bonuses (by their mask), flags and specials summed afresh, the
	 * weapon's attack and effect (Billy's two guns), an elemental weapon's element kept. Records the
	 * tables do not hold count as zero. The character is an id (0 to 10).
	 */
	WELTALLCORE_API void RecomputeCharacterEquipment(FGameState& State, int32 Character, const FEquipmentTables& Tables);

	/** The seven values the Status screen shows, in its order. */
	struct FCharacterStats
	{
		enum EStat : uint8
		{
			Attack,
			Hit,
			Defense,
			Evade,
			Ether,
			EtherDefense,
			Agility,
			Count
		};

		std::array<uint16, Count> Values{};
	};

	/**
	 * A character's stats as the Status screen shows them: base plus equipment (Billy's attack from his
	 * two guns, × 6 / 10), capped 250 (99 for hit and evade). Agility shows at most 16, as battle caps
	 * it; `bOriginalAgilityCap` shows it as the original does, where only a value over 20 becomes 16, or with
	 * bPerfectWorksRules as Perfect Works Build's rebalanced menu does, at most 20.
	 */
	WELTALLCORE_API FCharacterStats ComputeCharacterStats(const FGameState& State, int32 Character, bool bOriginalAgilityCap, bool bPerfectWorksRules = false);
}
