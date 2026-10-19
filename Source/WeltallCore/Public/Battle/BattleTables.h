#pragma once

#include "Battle/BattleCommand.h"
#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <array>
#include <span>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;

	/**
	 * The battle's tables from its data file: each character's commands, each gear's, the enemies'
	 * shared table and the items a battle can use.
	 */
	class FBattleTables
	{
	public:
		static constexpr int32 CharacterCount = 11;
		static constexpr int32 CharacterCommandCount = 38;
		/** Gears 0 to 18 have tables; a gear's table ends with command 40. */
		static constexpr int32 GearCount = 19;
		static constexpr int32 GearCommandCount = 41;
		static constexpr int32 EnemyCommandCount = 98;
		/** The items numbered below this are the ones a battle lists. */
		static constexpr int32 ItemCount = 48;
		/** The weapons the battle copies, from Billy's first ammunition on. */
		static constexpr int32 FirstAmmunition = 50;
		static constexpr int32 AmmunitionCount = 48;

		using FCharacterCommands = std::array<FBattleCommand, CharacterCommandCount>;
		using FGearCommands = std::array<FBattleCommand, GearCommandCount>;

		/** From the battle's data file as the game reads it: a container of LZSS sections. */
		WELTALLCORE_API static TResult<FBattleTables> Parse(std::span<const uint8> BattleDataFile);

		/** Read from the disc as the Battle program's setup reads them. */
		WELTALLCORE_API static TResult<FBattleTables> Load(const FCdImage& Image, const FGameFileIndex& Index);

		const FCharacterCommands& CharacterCommands(int32 Character) const { return Characters[static_cast<std::size_t>(Character)]; }
		const FGearCommands& GearCommands(int32 Gear) const { return Gears[static_cast<std::size_t>(Gear)]; }
		const std::array<FBattleCommand, EnemyCommandCount>& EnemyCommands() const { return Enemies; }
		const std::array<FBattleItem, ItemCount>& Items() const { return BattleItems; }
		/** Each copied weapon's attack: for Billy's ether ammunition, its element. */
		const std::array<uint8, AmmunitionCount>& AmmunitionAttacks() const { return Ammunition; }

	private:
		std::vector<FCharacterCommands> Characters;
		std::vector<FGearCommands> Gears;
		std::array<FBattleCommand, EnemyCommandCount> Enemies{};
		std::array<FBattleItem, ItemCount> BattleItems{};
		std::array<uint8, AmmunitionCount> Ammunition{};
	};
}
