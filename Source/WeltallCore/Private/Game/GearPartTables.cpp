#include "Game/GearPartTables.h"

#include "Formats/SectionContainer.h"
#include "Game/GameState.h"
#include "Game/GameStateLayout.h"
#include "Media/GameFileIndex.h"

#include <algorithm>
#include <string>

namespace Weltall
{
	namespace
	{
		/** The game's data file and its sections holding the tables, as the Menu reads them. */
		constexpr int32 GameDataDirectory = 16;
		constexpr int32 GameDataFileNumber = 2;
		constexpr uint32 FramesSection = 17;
		constexpr uint32 ArmorsSection = 18;
		constexpr uint32 EnginesSection = 19;
		constexpr uint32 PartsSection = 20;
		/** The menu program, and where in it each gear's pilot is listed (both discs of the USA release). */
		constexpr int32 MenuProgramFileNumber = 5;
		constexpr std::size_t PilotsOffset = 0x24808;

		std::span<const uint8> Record(const std::vector<uint8>& Table, std::size_t Size, uint8 Id)
		{
			const std::size_t Offset = Id * Size;
			return Offset + Size <= Table.size() ? std::span<const uint8>(Table).subspan(Offset, Size) : std::span<const uint8>();
		}

		TResult<std::vector<uint8>> ReadFile(const FCdImage& Image, const FGameFileIndex& Index, int32 Number)
		{
			const FIndexRecord* Record = Index.Find(GameDataDirectory, Number);
			if (Record == nullptr)
			{
				return MakeError("the disc lacks file " + std::to_string(Number) + " of directory 16");
			}
			return ReadFileSectors(Image, *Record);
		}
	}

	TResult<FGearPartTables> FGearPartTables::Make(std::vector<uint8> Frames, std::vector<uint8> Armors, std::vector<uint8> Engines,
		std::vector<uint8> Parts, std::array<uint8, GearCount> Pilots)
	{
		using namespace GameStateLayout;
		for (const uint8 Pilot : Pilots)
		{
			if (CharacterOffset(Pilot) + Character::Gear >= FGameState::BlockSize
				|| LearnedSkillsOffsetOf(Pilot) + Skills::Special + 2 > FGameState::BlockSize)
			{
				return MakeError("gear pilot " + std::to_string(Pilot) + " has no record in the game state");
			}
		}
		FGearPartTables Tables;
		Tables.Frames = std::move(Frames);
		Tables.Armors = std::move(Armors);
		Tables.Engines = std::move(Engines);
		Tables.Parts = std::move(Parts);
		Tables.Pilots = Pilots;
		return Tables;
	}

	bool FGearPartTables::SetPilot(int32 Gear, uint8 Pilot)
	{
		using namespace GameStateLayout;
		if (Gear < 0 || Gear >= GearCount || CharacterOffset(Pilot) + Character::Gear >= FGameState::BlockSize
			|| LearnedSkillsOffsetOf(Pilot) + Skills::Special + 2 > FGameState::BlockSize)
		{
			return false;
		}
		Pilots[static_cast<std::size_t>(Gear)] = Pilot;
		return true;
	}

	TResult<FGearPartTables> FGearPartTables::Parse(std::span<const uint8> GameDataFile, std::span<const uint8> MenuProgram)
	{
		std::array<std::vector<uint8>, 4> Tables;
		const std::array<uint32, 4> Sections{FramesSection, ArmorsSection, EnginesSection, PartsSection};
		for (std::size_t Table = 0; Table < Tables.size(); ++Table)
		{
			TResult<std::vector<uint8>> Unpacked = UnpackContainerSection(GameDataFile, Sections[Table]);
			if (!Unpacked)
			{
				return Unpacked.Error();
			}
			Tables[Table] = std::move(*Unpacked);
		}
		if (MenuProgram.size() < PilotsOffset + GearCount)
		{
			return MakeError("the menu program is too short to list the gears' pilots");
		}
		std::array<uint8, GearCount> Pilots;
		std::copy_n(MenuProgram.begin() + PilotsOffset, GearCount, Pilots.begin());
		return Make(std::move(Tables[0]), std::move(Tables[1]), std::move(Tables[2]), std::move(Tables[3]), Pilots);
	}

	TResult<FGearPartTables> FGearPartTables::Load(const FCdImage& Image, const FGameFileIndex& Index)
	{
		const TResult<std::vector<uint8>> GameData = ReadFile(Image, Index, GameDataFileNumber);
		if (!GameData)
		{
			return GameData.Error();
		}
		const TResult<std::vector<uint8>> MenuProgram = ReadFile(Image, Index, MenuProgramFileNumber);
		if (!MenuProgram)
		{
			return MenuProgram.Error();
		}
		TResult<FGearPartTables> Tables = Parse(*GameData, *MenuProgram);
		if (Tables)
		{
			Tables->SetPerfectWorksRules(Image.ModChanges().bPerfectWorksRebalance);
		}
		return Tables;
	}

	std::span<const uint8> FGearPartTables::Frame(uint8 Id) const
	{
		return Record(Frames, FrameSize, Id);
	}

	std::span<const uint8> FGearPartTables::Armor(uint8 Id) const
	{
		return Record(Armors, ArmorSize, Id);
	}

	std::span<const uint8> FGearPartTables::Engine(uint8 Id) const
	{
		return Record(Engines, EngineSize, Id);
	}

	std::span<const uint8> FGearPartTables::Part(uint8 Id) const
	{
		return Record(Parts, PartSize, Id);
	}
}
