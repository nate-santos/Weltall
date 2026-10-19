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

	/**
	 * What the Menu derives a gear's fields from: the frames, armors, engines and parts it can be
	 * built of, as records by id, and each gear's pilot.
	 */
	class FGearPartTables
	{
	public:
		static constexpr int32 GearCount = 20;
		static constexpr std::size_t FrameSize = 0x18;
		static constexpr std::size_t ArmorSize = 0x14;
		static constexpr std::size_t EngineSize = 0x10;
		static constexpr std::size_t PartSize = 0x1C;

		/**
		 * From the tables as the game unpacks them and the pilots by gear. A pilot is a character
		 * id whose record and learned skills the recompute reads and writes; one whose records
		 * would lie outside the game state is refused.
		 */
		WELTALLCORE_API static TResult<FGearPartTables> Make(std::vector<uint8> Frames, std::vector<uint8> Armors, std::vector<uint8> Engines,
			std::vector<uint8> Parts, std::array<uint8, GearCount> Pilots);

		/** From the game's data file, whose LZSS sections hold the tables, and the menu program, which lists the pilots. */
		WELTALLCORE_API static TResult<FGearPartTables> Parse(std::span<const uint8> GameDataFile, std::span<const uint8> MenuProgram);

		/** Read from the disc as the Menu reads them. */
		WELTALLCORE_API static TResult<FGearPartTables> Load(const FCdImage& Image, const FGameFileIndex& Index);

		/** A record by id; empty when the table's data ends before it. */
		WELTALLCORE_API std::span<const uint8> Frame(uint8 Id) const;
		WELTALLCORE_API std::span<const uint8> Armor(uint8 Id) const;
		WELTALLCORE_API std::span<const uint8> Engine(uint8 Id) const;
		WELTALLCORE_API std::span<const uint8> Part(uint8 Id) const;

		/** The character who pilots a gear. */
		uint8 Pilot(int32 Gear) const { return Pilots[static_cast<std::size_t>(Gear)]; }

		/**
		 * The Menu's own copy of the table changes a gear's pilot (the adult Emeralda takes Crescens once
		 * she has joined); refused, as by Make, for a pilot whose records lie outside the game state.
		 */
		WELTALLCORE_API bool SetPilot(int32 Gear, uint8 Pilot);

		/**
		 * The tables are Perfect Works Build's rebalance's, whose recompute reads a part's defenses as words and gives its
		 * Deathblow parts' tiers by flags, in the Menu's copy and the gear shop's.
		 */
		bool UsesPerfectWorksRules() const { return bPerfectWorksRules; }
		void SetPerfectWorksRules(bool bOn) { bPerfectWorksRules = bOn; }

	private:
		std::vector<uint8> Frames;
		std::vector<uint8> Armors;
		std::vector<uint8> Engines;
		std::vector<uint8> Parts;
		std::array<uint8, GearCount> Pilots{};
		bool bPerfectWorksRules = false;
	};
}
