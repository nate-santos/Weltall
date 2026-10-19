#include "Game/GearRecompute.h"

#include "Foundation/LittleEndian.h"
#include "Foundation/Sha1.h"
#include "Game/GameState.h"
#include "Game/GearPartTables.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"
#include "Support/GameText.h"
#include "Support/XorShift32.h"

#include <catch_amalgamated.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace Weltall;

namespace
{
	constexpr std::size_t GearsOffset = 0x978;
	constexpr std::size_t GearSize = 0xA4;
	constexpr std::size_t SkillsOffset = 0x16C0;

	std::vector<uint8> RandomBytes(Testing::FXorShift32& Random, std::size_t Size)
	{
		std::vector<uint8> Bytes(Size);
		for (std::size_t Offset = 0; Offset < Size; Offset += 4)
		{
			LittleEndian::WriteU32(Bytes, Offset, Random.Next());
		}
		return Bytes;
	}

	std::span<uint8> Gear(FGameState& State, int32 Id)
	{
		return State.Block().subspan(GearsOffset + static_cast<std::size_t>(Id) * GearSize, GearSize);
	}

	/** Frame, armor and engine 1 of plain values; parts 1-5, most with a special. Every gear's pilot is Fei. */
	FGearPartTables MakeTables()
	{
		std::vector<uint8> Frames(2 * FGearPartTables::FrameSize, 0);
		LittleEndian::WriteU32(Frames, 0x18 + 4, 1800);
		Frames[0x18 + 0x14] = 7;
		std::vector<uint8> Armors(2 * FGearPartTables::ArmorSize, 0);
		LittleEndian::WriteU16(Armors, 0x14 + 8, 300);
		LittleEndian::WriteU16(Armors, 0x14 + 0xA, 200);
		std::vector<uint8> Engines(2 * FGearPartTables::EngineSize, 0);
		LittleEndian::WriteU16(Engines, 0x10 + 6, 1200);
		Engines[0x10 + 0xC] = 4;
		Engines[0x10 + 0xE] = 10;
		std::vector<uint8> Parts(7 * FGearPartTables::PartSize, 0);
		auto Part = [&Parts](std::size_t Id, uint16 Weight, uint8 Special, uint16 Value)
		{
			const std::size_t Record = Id * FGearPartTables::PartSize;
			LittleEndian::WriteU16(Parts, Record + 6, Weight);
			Parts[Record + 0xD] = 5;
			Parts[Record + 0x15] = Special;
			LittleEndian::WriteU16(Parts, Record + 0x16, Value);
			Parts[Record + 0x1A] = 3;
		};
		Part(1, 1000, 9, 0x0102);
		Part(2, 1000, 4, 0x8001);
		Part(3, 1000, 5, 3);
		Part(4, 0, 6, 0);
		Part(5, 0xFFFF, 0, 0);
		std::array<uint8, FGearPartTables::GearCount> Pilots{};
		auto Tables = FGearPartTables::Make(std::move(Frames), std::move(Armors), std::move(Engines), std::move(Parts), Pilots);
		REQUIRE(Tables);
		return std::move(*Tables);
	}

	/** A gear of frame, armor and engine 1 and the given parts. */
	void Build(FGameState& State, int32 Id, std::array<uint8, 3> Parts)
	{
		const std::span<uint8> Record = Gear(State, Id);
		Record[2] = 1;
		Record[3] = 1;
		Record[8] = 1;
		std::copy(Parts.begin(), Parts.end(), Record.begin() + 9);
	}
}

TEST_CASE("A gear's frame, armor and engine give its HP, defenses, fuel and output", "[Game]")
{
	const FGearPartTables Tables = MakeTables();
	FGameState State;
	Build(State, 0, {0, 0, 0});
	const std::span<uint8> Record = Gear(State, 0);
	LittleEndian::WriteU32(Record, 0x60, 5);
	LittleEndian::WriteU16(Record, 0x38, 5000);
	REQUIRE(ApplyGearFrame(State, 0, Tables));
	REQUIRE(ApplyGearArmor(State, 0, Tables));
	REQUIRE(ApplyGearEngine(State, 0, Tables));
	// The frame repairs the gear fully; the engine caps its fuel.
	CHECK(LittleEndian::ReadU32(Record, 0x60) == 1800);
	CHECK(LittleEndian::ReadU32(Record, 0x64) == 1800);
	CHECK(Record[0x98] == 7);
	CHECK(LittleEndian::ReadU16(Record, 0x70) == 300);
	CHECK(LittleEndian::ReadU16(Record, 0x72) == 200);
	CHECK(LittleEndian::ReadU16(Record, 0x38) == 1200);
	CHECK(LittleEndian::ReadU16(Record, 0x3A) == 1200);
	CHECK(Record[0x3C] == 4);
	CHECK(Record[0x3E] == 10);
	CHECK(Record[0x3F] == 10);
}

TEST_CASE("A gear's parts sum, grant their specials and set the overweight penalty", "[Game]")
{
	const FGearPartTables Tables = MakeTables();
	FGameState State;
	Build(State, 0, {1, 2, 3});
	std::span<uint8> Record = Gear(State, 0);
	Record[0x56] = 50;
	Record[0x75] = 5;
	LittleEndian::WriteU16(Record, 0x86, 0xF0FF);
	const std::size_t FeiSkills = SkillsOffset;
	LittleEndian::WriteU16(State.Block(), FeiSkills + 4, 0xFFFF);
	REQUIRE(ApplyGearParts(State, 0, Tables));
	CHECK(LittleEndian::ReadU16(Record, 0x40) == 15);
	CHECK(LittleEndian::ReadU16(Record, 0x44) == 3000);
	// Special 9 also adds its value's low byte to the attack multiplier bonus, as special 10 does.
	CHECK(LittleEndian::ReadU16(Record, 0x48) == 0x0102);
	CHECK(Record[0x56] == 2);
	CHECK(LittleEndian::ReadU16(Record, 0x6E) == 0x8001);
	CHECK(Record[0x88] == 3);
	CHECK(Record[0x97] == 3);
	CHECK(Record[0x89] == 0);
	CHECK(Record[0x4F] == 3);
	CHECK(LittleEndian::ReadU16(Record, 0x86) == 0xF000);
	// (3000 / 120 - 5) / 2.
	CHECK(Record[0x4A] == 10);
	// The parts' Deathblows are cleared first, then only granted by their parts; the repair ability is set.
	CHECK(LittleEndian::ReadU16(State.Block(), FeiSkills + 4) == 0xFB6F);
	CHECK(LittleEndian::ReadU16(State.Block(), FeiSkills + 0x1A) == 0x8000);

	SECTION("a Deathblow part grants its tier's third once the other two are known")
	{
		Build(State, 0, {4, 0, 0});
		REQUIRE(ApplyGearParts(State, 0, Tables));
		CHECK(LittleEndian::ReadU16(State.Block(), FeiSkills + 4) == 0xFF6F);
		LittleEndian::WriteU16(State.Block(), FeiSkills + 4, 0x1000);
		REQUIRE(ApplyGearParts(State, 0, Tables));
		CHECK(LittleEndian::ReadU16(State.Block(), FeiSkills + 4) == 0x1000);
	}
	SECTION("the repair ability is taken away only by the gear the pilot is in")
	{
		Build(State, 1, {0, 0, 0});
		REQUIRE(ApplyGearParts(State, 1, Tables));
		CHECK(LittleEndian::ReadU16(State.Block(), FeiSkills + 0x1A) == 0x8000);
		State.Block()[0x26C + 0xA0] = 1;
		REQUIRE(ApplyGearParts(State, 1, Tables));
		CHECK(LittleEndian::ReadU16(State.Block(), FeiSkills + 0x1A) == 0);
	}
	SECTION("the weight is read unsigned, and a light gear has no penalty")
	{
		Build(State, 0, {5, 0, 0});
		REQUIRE(ApplyGearParts(State, 0, Tables));
		CHECK(Record[0x4A] == static_cast<uint8>((0xFFFF / 120 - 5) / 2));
		Build(State, 0, {0, 0, 0});
		REQUIRE(ApplyGearParts(State, 0, Tables));
		CHECK(Record[0x4A] == 0);
	}
}

TEST_CASE("The gear shop fits a frame without repairing and sums the parts by its own copy", "[Game]")
{
	// Frame 1 of 1800 HP and weight 801; part 1 an ether amplifier of 4 and a first-tier Deathblow special.
	std::vector<uint8> Frames(2 * FGearPartTables::FrameSize, 0);
	LittleEndian::WriteU32(Frames, 0x18 + 4, 1800);
	LittleEndian::WriteU16(Frames, 0x18 + 8, 801);
	std::vector<uint8> Parts(2 * FGearPartTables::PartSize, 0);
	Parts[FGearPartTables::PartSize + 0x15] = 6;
	Parts[FGearPartTables::PartSize + 0x1B] = 4;
	auto Tables = FGearPartTables::Make(std::move(Frames), std::vector<uint8>(2 * FGearPartTables::ArmorSize, 0),
		std::vector<uint8>(2 * FGearPartTables::EngineSize, 0), std::move(Parts), {});
	REQUIRE(Tables);
	FGameState State;
	Build(State, 0, {1, 0, 0});
	std::span<uint8> Record = Gear(State, 0);
	LittleEndian::WriteU32(Record, 0x60, 2000);
	REQUIRE(RefitGearFrame(State, 0, *Tables));
	// The maximum set and HP only capped to it; the frame's weight.
	CHECK(LittleEndian::ReadU32(Record, 0x60) == 1800);
	CHECK(LittleEndian::ReadU32(Record, 0x64) == 1800);
	CHECK(LittleEndian::ReadU16(Record, 0x68) == 801);
	LittleEndian::WriteU32(Record, 0x60, 5);
	REQUIRE(RefitGearFrame(State, 0, *Tables));
	CHECK(LittleEndian::ReadU32(Record, 0x60) == 5);

	// On the pilot given: the shop's copy clears its own Deathblow bits and leaves the ether amplifier out.
	const std::size_t Skills = SkillsOffset + 3 * 0x20;
	LittleEndian::WriteU16(State.Block(), Skills + 4, 0xFFFF);
	REQUIRE(ApplyGearParts(State, 0, *Tables, 3, EGearPartsCopy::GearShop));
	CHECK(LittleEndian::ReadU16(State.Block(), Skills + 4) == 0xDF7F);
	CHECK(Record[0x54] == 0);
	LittleEndian::WriteU16(State.Block(), Skills + 4, 0xFFFF);
	REQUIRE(ApplyGearParts(State, 0, *Tables, 3, EGearPartsCopy::Menu));
	CHECK(LittleEndian::ReadU16(State.Block(), Skills + 4) == 0xFF6F);
	CHECK(Record[0x54] == 4);
	CHECK_FALSE(ApplyGearParts(State, 0, *Tables, 0xFF, EGearPartsCopy::Menu));
}

// Perfect Works Build's parts keep their defenses as words at +0Ch and +0Eh; under its rebalance both copies read them so, and
// the original's rules read the bytes +0Dh and +0Eh (400 as 1, 300 as 44).
TEST_CASE("The gear shop's copy reads Perfect Works Build's part defenses as the Menu's does", "[Game]")
{
	std::vector<uint8> Parts(2 * FGearPartTables::PartSize, 0);
	LittleEndian::WriteU16(Parts, FGearPartTables::PartSize + 0x0C, 400);
	LittleEndian::WriteU16(Parts, FGearPartTables::PartSize + 0x0E, 300);
	auto Tables = FGearPartTables::Make(std::vector<uint8>(2 * FGearPartTables::FrameSize, 0), std::vector<uint8>(2 * FGearPartTables::ArmorSize, 0),
		std::vector<uint8>(2 * FGearPartTables::EngineSize, 0), std::move(Parts), {});
	REQUIRE(Tables);
	FGameState State;
	Build(State, 0, {1, 0, 0});
	const std::span<uint8> Record = Gear(State, 0);
	const auto Defenses = [&](EGearPartsCopy Copy)
	{
		REQUIRE(ApplyGearParts(State, 0, *Tables, 3, Copy));
		return std::pair{LittleEndian::ReadU16(Record, 0x40), LittleEndian::ReadU16(Record, 0x42)};
	};
	Tables->SetPerfectWorksRules(true);
	CHECK(Defenses(EGearPartsCopy::GearShop) == std::pair{uint16{400}, uint16{300}});
	CHECK(Defenses(EGearPartsCopy::Menu) == std::pair{uint16{400}, uint16{300}});
	Tables->SetPerfectWorksRules(false);
	CHECK(Defenses(EGearPartsCopy::GearShop) == std::pair{uint16{1}, uint16{44}});
}

TEST_CASE("A gear naming a record the tables lack is left as it is", "[Game]")
{
	const FGearPartTables Tables = MakeTables();
	FGameState State;
	Build(State, 0, {1, 7, 1});
	Gear(State, 0)[2] = 2;
	Gear(State, 0)[3] = 2;
	Gear(State, 0)[8] = 2;
	CHECK_FALSE(ApplyGearFrame(State, 0, Tables));
	CHECK_FALSE(ApplyGearArmor(State, 0, Tables));
	CHECK_FALSE(ApplyGearEngine(State, 0, Tables));
	CHECK_FALSE(ApplyGearParts(State, 0, Tables));
	CHECK(State.Block()[GearsOffset + 0x40] == 0);
	CHECK_FALSE(FGearPartTables::Make({}, {}, {}, {}, {52}));
}

namespace
{
	/** The recompute's four functions run on the oracle's random tables and states, each one's digest. */
	std::array<std::string, 4> RecomputeDigests(bool bPerfectWorksRules)
	{
		using FApply = bool (*)(FGameState&, int32, const FGearPartTables&);
		const std::array<FApply, 4> Functions{&ApplyGearFrame, &ApplyGearArmor, &ApplyGearEngine, &ApplyGearParts};
		constexpr std::size_t Records = 256;
		constexpr int32 CasesPerFunction = 200;
		Testing::FXorShift32 Random(0x5A7E1D0C);
		std::vector<uint8> Frames = RandomBytes(Random, Records * FGearPartTables::FrameSize);
		std::vector<uint8> Armors = RandomBytes(Random, Records * FGearPartTables::ArmorSize);
		std::vector<uint8> Engines = RandomBytes(Random, Records * FGearPartTables::EngineSize);
		std::vector<uint8> Parts = RandomBytes(Random, Records * FGearPartTables::PartSize);
		for (std::size_t Record = 0; Record < Records; ++Record)
		{
			Parts[Record * FGearPartTables::PartSize + 0x15] = static_cast<uint8>(Random.Next() % 13);
		}
		std::array<uint8, FGearPartTables::GearCount> Pilots;
		for (uint8& Pilot : Pilots)
		{
			Pilot = static_cast<uint8>(Random.Next() % 16);
		}
		auto Tables = FGearPartTables::Make(std::move(Frames), std::move(Armors), std::move(Engines), std::move(Parts), Pilots);
		REQUIRE(Tables);
		Tables->SetPerfectWorksRules(bPerfectWorksRules);
		std::array<std::string, 4> Digests;
		for (std::size_t Function = 0; Function < Functions.size(); ++Function)
		{
			FSha1 Digest;
			for (int32 Index = 0; Index < CasesPerFunction; ++Index)
			{
				FGameState State;
				const std::vector<uint8> Block = RandomBytes(Random, FGameState::BlockSize);
				std::copy(Block.begin(), Block.end(), State.Block().begin());
				REQUIRE(Functions[Function](State, static_cast<int32>(Random.Next() % FGearPartTables::GearCount), *Tables));
				Digest.Update(State.Block());
			}
			Digests[Function] = FSha1::ToHex(Digest.Finish());
		}
		return Digests;
	}
}

// From the original functions under the oracle harness (save_digests), drawing the same cases.
TEST_CASE("The gear recompute leaves what the Menu's own leaves on random parts and states", "[Game]")
{
	CHECK(RecomputeDigests(false) == std::array<std::string, 4>{"13aa542c8030bf88c95fabdb5a0ad70c7a66ff5c", "ec08e633481226ac8d6bd6ceb02d717a6bbb027c",
		"a3c214a3a02f01cc4ed706e7d2c0246cd4efbbfa", "bd88eba33641289afce2e118fc27e8d756a424a3"});
}

// The same with Perfect Works Build's rebalanced party's MENU_MAIN (save_digests --perfect-works-rebalance): only the parts differ.
TEST_CASE("The gear recompute leaves what Perfect Works Build's rebalanced Menu leaves", "[Game]")
{
	CHECK(RecomputeDigests(true) == std::array<std::string, 4>{"13aa542c8030bf88c95fabdb5a0ad70c7a66ff5c", "ec08e633481226ac8d6bd6ceb02d717a6bbb027c",
		"a3c214a3a02f01cc4ed706e7d2c0246cd4efbbfa", "0f1436a214c3cc3e5aad00c8ff8c0fe420dca941"});
}

TEST_CASE("The disc's tables recompute the new game's gears as the Menu does", "[Game][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	const auto Tables = FGearPartTables::Load(Disc->Image(), *Index);
	REQUIRE(Tables);
	CHECK(Tables->Pilot(0) == 0);
	CHECK(Tables->Pilot(9) == 6);
	CHECK(Tables->Pilot(19) == 15);
	auto State = LoadNewGameState(Disc->Image(), *Index, Resident.NameTables);
	REQUIRE(State);
	for (int32 Id = 0; Id < FGearPartTables::GearCount; ++Id)
	{
		REQUIRE(ApplyGearFrame(*State, Id, *Tables));
		REQUIRE(ApplyGearArmor(*State, Id, *Tables));
		REQUIRE(ApplyGearEngine(*State, Id, *Tables));
		REQUIRE(ApplyGearParts(*State, Id, *Tables));
	}
	// The Menu's own recompute under the oracle harness, from the kernel's new game.
	CHECK(FSha1::ToHex(FSha1::Hash(State->Block())) == "b0bb9ee6b4e4dc8fc2913f9d5b6eb82ffda756bf");
	CHECK(LittleEndian::ReadU32(Gear(*State, 0), 0x64) == 1800);
}
