#include "Battle/BattleContext.h"
#include "Battle/BattleEvent.h"
#include "Game/GameState.h"
#include "Game/PsxRandom.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <initializer_list>
#include <memory>
#include <set>
#include <vector>

using namespace Weltall;

namespace
{
	/** A script of the given actors' entry points and code, laid out as the archive's are. */
	std::vector<uint8> MakeScript(std::initializer_list<std::initializer_list<uint16>> Entries, std::initializer_list<uint8> Code)
	{
		std::vector<uint8> Script(BattleEventScript::ActorCountOffset, 0);
		const uint32 Actors = static_cast<uint32>(Entries.size());
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			Script.push_back(static_cast<uint8>(Actors >> Shift));
		}
		for (const auto& Row : Entries)
		{
			std::vector<uint16> Points(Row);
			Points.resize(BattleEventScript::EntryCount, 0);
			for (const uint16 Point : Points)
			{
				Script.push_back(static_cast<uint8>(Point));
				Script.push_back(static_cast<uint8>(Point >> 8));
			}
		}
		Script.insert(Script.end(), Code);
		return Script;
	}

	/** One message, "A", in a table of the kernel's format. */
	const std::vector<uint8> OneMessage = {0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x01, 0x21, 0x00};

	struct FEventBattle
	{
		FGameState State;
		FPsxRandom Random{7};
		std::unique_ptr<FBattleContext> Context = std::make_unique<FBattleContext>();
		std::vector<uint8> Script;
		int32 Frames = 0;

		explicit FEventBattle(std::vector<uint8> InScript, const FBattleEventOptions& Options = FBattleEventOptions::Original()) : Script(std::move(InScript))
		{
			Context->GameState = &State;
			Context->Random = &Random;
			Context->EventOptions = Options;
			BeginBattleEvent(*Context, Script, OneMessage);
			Finish();
		}

		/** The routine to its end, counting its frames. */
		void Finish()
		{
			while (StepBattleEvent(*Context))
			{
				++Frames;
			}
		}

		void Run()
		{
			BeginBattleEventRun(*Context);
			Finish();
		}
	};
}

namespace
{
	/**
	 * Actor 0: Start actor 1 script 2, StartWaitDone actor 2 script 2, var 7 = 1, Return, End. Actors 1 and 2's
	 * scripts 2 count vars 5 and 6; every idle script returns.
	 */
	std::vector<uint8> StartThenWaitedStart()
	{
		return MakeScript({{0x0000, 0x0016}, {0x0016, 0x0016, 0x000E}, {0x0016, 0x0016, 0x0012}},
			{0x03, 0x01, 0x62, 0x05, 0x02, 0x62, 0x06, 0x0E, 0x00, 0x01, 0x00, 0x40, 0x22, 0x00,
			 0x0D, 0x0A, 0x00, 0x00,
			 0x0D, 0x0C, 0x00, 0x00,
			 0x22, 0x00});
	}
}

TEST_CASE("A waited start of a script the actor has just started on another actor is skipped in the original", "[Battle]")
{
	FEventBattle Battle(StartThenWaitedStart());
	FBattleContext& Context = *Battle.Context;
	for (int32 Hook = 0; Hook < 3; ++Hook)
	{
		Battle.Run();
		REQUIRE(Context.Fault == EBattleFault::None);
	}
	CHECK(Context.EventVariables[5] == 1);
	CHECK(Context.EventVariables[6] == 0);
	CHECK(Context.EventVariables[7] == 1);
}

TEST_CASE("With the fix a waited start starts the script of the actor it names", "[Battle]")
{
	FEventBattle Battle(StartThenWaitedStart(), FBattleEventOptions{});
	FBattleContext& Context = *Battle.Context;
	for (int32 Hook = 0; Hook < 3; ++Hook)
	{
		Battle.Run();
		REQUIRE(Context.Fault == EBattleFault::None);
	}
	CHECK(Context.EventVariables[5] == 1);
	CHECK(Context.EventVariables[6] == 1);
	CHECK(Context.EventVariables[7] == 1);
}

TEST_CASE("SetOrder moves the actor to the front and steps over the byte after its operand", "[Battle]")
{
	// Actor 1: SetOrder FEh, then a byte the pc passes (an Inc's opcode, which would count var 1), Inc var 2,
	// Return, End.
	FEventBattle Battle(MakeScript({{0x0000, 0x0000}, {0x0001, 0x0001}},
		{0x00,
		 0x2C, 0xFE, 0x0D, 0x0D, 0x04, 0x00, 0x22, 0x00}));
	FBattleContext& Context = *Battle.Context;
	Battle.Run();
	REQUIRE(Context.Fault == EBattleFault::None);
	CHECK(Context.Event.Order[0] == 1);
	CHECK(Context.Event.Order[1] == 0);
	CHECK(Context.EventVariables[1] == 0);
	CHECK(Context.EventVariables[2] >= 1);
}

TEST_CASE("Battle events stop where the game would hang or read past its data", "[Battle]")
{
	SECTION("a message whose window opens at its size before any window has grown")
	{
		// The default window style (1F0h) draws the window at its size, and the message waits for a growth nothing starts.
		FEventBattle Battle(MakeScript({{0x0000, 0x0000}}, {0x18, 0x00, 0x00, 0x00, 0x22, 0x00}));
		Battle.Run();
		CHECK(Battle.Context->Fault == EBattleFault::EventHangs);
	}
	SECTION("an opcode past the handlers")
	{
		FEventBattle Battle(MakeScript({{0x0000, 0x0000}}, {0x4C, 0x22, 0x00}));
		Battle.Run();
		CHECK(Battle.Context->Fault == EBattleFault::UnknownEventOpcode);
	}
	SECTION("a variable past the event's 512")
	{
		FEventBattle Battle(MakeScript({{0x0000, 0x0000}}, {0x0D, 0x00, 0x04, 0x22, 0x00}));
		Battle.Run();
		CHECK(Battle.Context->Fault == EBattleFault::EventVariableOutsideWork);
	}
	SECTION("a script with more actors than the work has records")
	{
		std::vector<uint8> Script(BattleEventScript::ActorCountOffset, 0);
		Script.insert(Script.end(), {17, 0, 0, 0});
		Script.resize(Script.size() + 17 * 16 + 1, 0);
		FEventBattle Battle(Script);
		CHECK(Battle.Context->Fault == EBattleFault::EventRecordOutsideWork);
	}
}

TEST_CASE("Every battle event script on the disc decodes along every thread", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	const auto Archive = FBattleEventArchive::Load(Disc->Image(), *Index);
	REQUIRE(Archive);
	REQUIRE(Archive->Count() == 48);
	for (int32 Number = 0; Number < Archive->Count(); ++Number)
	{
		INFO("script " << Number);
		const std::span<const uint8> Script = Archive->Script(Number);
		REQUIRE(Script.size() > BattleEventScript::EntriesOffset);
		const uint32 Actors = Script[0x40] | Script[0x41] << 8 | Script[0x42] << 16 | static_cast<uint32>(Script[0x43]) << 24;
		REQUIRE(Actors >= 1);
		REQUIRE(Actors <= 16);
		const std::size_t CodeStart = BattleEventScript::EntriesOffset + 0x10 * Actors;
		REQUIRE(CodeStart < Script.size());
		const std::span<const uint8> Code = Script.subspan(CodeStart);
		std::vector<std::size_t> Pending{0};
		for (uint32 Actor = 0; Actor < Actors; ++Actor)
		{
			for (int32 Entry = 0; Entry < BattleEventScript::EntryCount; ++Entry)
			{
				const std::size_t At = BattleEventScript::EntriesOffset + 0x10 * Actor + 2 * static_cast<std::size_t>(Entry);
				Pending.push_back(static_cast<std::size_t>(Script[At] | Script[At + 1] << 8));
			}
		}
		std::set<std::size_t> Starts;
		std::set<std::size_t> Inside;
		while (!Pending.empty())
		{
			std::size_t Offset = Pending.back();
			Pending.pop_back();
			while (!Starts.contains(Offset))
			{
				REQUIRE(Offset < Code.size());
				const uint8 Opcode = Code[Offset];
				const std::size_t Length = BattleEventScript::InstructionLength(Opcode);
				REQUIRE(Length != 0);
				REQUIRE(Offset + Length <= Code.size());
				Starts.insert(Offset);
				for (std::size_t Byte = Offset + 1; Byte < Offset + Length; ++Byte)
				{
					Inside.insert(Byte);
				}
				uint16 Target = 0;
				if (BattleEventScript::JumpTarget(Code, Offset, Target))
				{
					Pending.push_back(Target);
				}
				if (Opcode == 0x00 || Opcode == 0x01)
				{
					break;
				}
				Offset += Length;
			}
		}
		for (const std::size_t Start : Starts)
		{
			CHECK_FALSE(Inside.contains(Start));
		}
	}
	CHECK(Archive->Script(0)[BattleEventScript::EntriesOffset + 0x10 * Archive->Script(0)[0x40]] == 0x37);
}
