#include "Battle/BattleAi.h"
#include "Battle/BattleContext.h"
#include "Game/GameState.h"
#include "Game/PsxRandom.h"

#include <catch_amalgamated.hpp>

#include <initializer_list>
#include <memory>
#include <vector>

using namespace Weltall;

namespace
{
	/** Two enemies (slots 3 and 4) whose turn scripts are the words given, one after the other in the scene's file. */
	struct FAiBattle
	{
		FGameState State;
		FPsxRandom Random{7};
		std::vector<uint8> Scene;
		std::unique_ptr<FBattleContext> Context = std::make_unique<FBattleContext>();

		FAiBattle(std::initializer_list<uint32> First, std::initializer_list<uint32> Second = {0xFFFFFFFD})
		{
			Context->GameState = &State;
			Context->Random = &Random;
			for (const std::size_t Slot : {std::size_t{3}, std::size_t{4}})
			{
				Context->InBattle[Slot] = true;
			}
			Append(First, 0);
			Append(Second, 1);
			Context->SceneFile = Scene;
		}

		void Append(std::initializer_list<uint32> Words, std::size_t Enemy)
		{
			Context->AiRecords[Enemy].Scripts[0] = static_cast<uint32>(Scene.size());
			for (const uint32 Word : Words)
			{
				for (int32 Byte = 0; Byte < 4; ++Byte)
				{
					Scene.push_back(static_cast<uint8>(Word >> (8 * Byte)));
				}
			}
		}

		uint8 B(std::size_t Enemy, std::size_t Index) const { return Context->AiRecords[Enemy].Variables[0x20 + Index]; }
	};

	/** A script word from its opcode and operands, as the scene's file holds it. */
	constexpr uint32 W(uint32 Op, uint32 A = 0, uint32 B = 0, uint32 C = 0)
	{
		return Op | A << 8 | B << 16 | C << 24;
	}

	constexpr uint32 End = W(0xFD, 0xFF, 0xFF, 0xFF);
	constexpr uint32 Stop = W(0xFF, 0xFF, 0xFF, 0xFF);
}

TEST_CASE("An enemy's variable index past its record reaches the next enemies' variables", "[Battle]")
{
	FAiBattle Battle({W(0x80), W(0x04, 0x40 + 2, 0x5A), W(0x05, 0x20 + 1, 0x34, 0x12), W(0x06, 0x10 + 3, 0x01), End, Stop});
	RunBattleTurnScript(*Battle.Context, 3);
	REQUIRE(Battle.Context->Fault == EBattleFault::None);
	const auto& Next = Battle.Context->AiRecords[1].Variables;
	CHECK(Battle.B(1, 2) == 0x5A);
	CHECK((Next[0x10 + 2] | Next[0x10 + 3] << 8) == 0x1234);
	CHECK(Next[0x0C] == 0x10);
}

TEST_CASE("An enemy's script stops where the game would reach memory the battle does not model", "[Battle]")
{
	SECTION("a variable over the next enemy's scripts")
	{
		FAiBattle Battle({W(0x80), W(0x04, 0x10, 1), End, Stop});
		RunBattleTurnScript(*Battle.Context, 3);
		CHECK(Battle.Context->Fault == EBattleFault::AiVariableOutsideRecords);
	}
	SECTION("a variable past the eighth enemy's record")
	{
		FAiBattle Battle({W(0x80), W(0x04, 0x40 * 3 + 0x10, 1), End, Stop});
		Battle.Context->AiRecords[5].Scripts[0] = Battle.Context->AiRecords[0].Scripts[0];
		Battle.Context->InBattle[8] = true;
		RunBattleTurnScript(*Battle.Context, 8);
		CHECK(Battle.Context->Fault == EBattleFault::AiVariableOutsideRecords);
	}
	SECTION("a script whose last block is false, run past the scene's file")
	{
		FAiBattle Battle({W(0x81, 0, 1), W(0x04, 0, 2), End}, {W(0x81, 0, 1), End});
		Battle.Context->AiRecords[1].Scripts[0] = Battle.Context->AiRecords[0].Scripts[0];
		RunBattleTurnScript(*Battle.Context, 4);
		CHECK(Battle.Context->Fault == EBattleFault::ScriptOutsideScene);
	}
	SECTION("a slot read through an empty mask")
	{
		FAiBattle Battle({W(0x80), W(0x2A, 0, 5, 3), End, Stop});
		RunBattleTurnScript(*Battle.Context, 3);
		CHECK(Battle.Context->Fault == EBattleFault::SlotOutsideBattle);
	}
	SECTION("the battle event's variables in a battle without an event")
	{
		FAiBattle Battle({W(0x80), W(0x70, 0, 1), End, Stop});
		RunBattleTurnScript(*Battle.Context, 3);
		CHECK(Battle.Context->Fault == EBattleFault::NoBattleEvent);
	}
	SECTION("a transformation into no one")
	{
		FAiBattle Battle({W(0x80), W(0x01, 0, 7), End, Stop});
		RunBattleTurnScript(*Battle.Context, 3);
		REQUIRE(Battle.Context->Fault == EBattleFault::None);
		ExecuteBattleAiQueue(*Battle.Context, 3);
		CHECK(Battle.Context->Fault == EBattleFault::SlotOutsideBattle);
	}
}

TEST_CASE("A false last block runs on into the next script, as in the game", "[Battle]")
{
	FAiBattle Battle({W(0x81, 0, 1), W(0x04, 0, 2), End}, {W(0x80), W(0x04, 1, 9), End, Stop});
	RunBattleTurnScript(*Battle.Context, 3);
	REQUIRE(Battle.Context->Fault == EBattleFault::None);
	CHECK(Battle.B(0, 0) == 0);
	CHECK(Battle.B(0, 1) == 9);
}

TEST_CASE("A queue without an empty entry is carried out past its end", "[Battle]")
{
	FAiBattle Battle({W(0x80), End, Stop});
	for (std::size_t Entry = 0; Entry < 32; ++Entry)
	{
		Battle.Context->AiQueue[Entry * 8] = 0x05;
	}
	ExecuteBattleAiQueue(*Battle.Context, 3);
	CHECK(Battle.Context->Fault == EBattleFault::EntryOutsideQueue);
}

TEST_CASE("The group of an empty mask's slot is the first show record's amount, which the slot records run into", "[Battle]")
{
	FAiBattle Battle({W(0x80), W(0x67, 1, 0), End, Stop});
	FBattleContext& Context = *Battle.Context;
	for (uint8 Slot = 0; Slot < 3; ++Slot)
	{
		Context.InBattle[Slot] = true;
		Context.Slots[Slot].Group = Slot;
	}
	Context.Performances[0].Amounts[0] = 0x0301;
	RunBattleTurnScript(Context, 3);
	REQUIRE(Context.Fault == EBattleFault::None);
	const auto& Words = Context.AiRecords[0].Variables;
	CHECK((Words[0x10 + 2] | Words[0x10 + 3] << 8) == 0x0002);
}

TEST_CASE("An empty mask's slot reads and writes what lies after the combatants where the game's scripts do", "[Battle]")
{
	SECTION("its gear HP is four bytes of the first member's fourth command")
	{
		// L1 = slot(W0).gear HP; L0 = 0x1234 * 16; slot(W0).gear HP = L0; L2 = slot(W0).gear HP, with W0 never set.
		FAiBattle Battle({W(0x80), W(0x2E, 1, 1, 0), W(0x06, 0, 0x34, 0x12), W(0x2F, 0, 1, 0), W(0x2E, 2, 1, 0), End, Stop});
		FBattleContext& Context = *Battle.Context;
		FBattleCommand& Fourth = Context.Commands[FBattleContext::PartyCommandIndex(0, 3)];
		Fourth.Bytes04 = 0xCAFE0001;
		const uint16 Flags = Fourth.Flags;
		RunBattleTurnScript(Context, 3);
		REQUIRE(Context.Fault == EBattleFault::None);
		const auto Long = [&Context](std::size_t Index)
		{
			const auto& Variables = Context.AiRecords[0].Variables;
			return static_cast<uint32>(Variables[4 * Index] | Variables[4 * Index + 1] << 8 | Variables[4 * Index + 2] << 16) | static_cast<uint32>(Variables[4 * Index + 3]) << 24;
		};
		CHECK(Long(1) == 0xCAFE0001);
		CHECK(Long(2) == 0x12340);
		CHECK(Fourth.Bytes04 == 0x12340);
		CHECK(Fourth.Flags == Flags);
		// No party slot was named: the queue's first entry is not poisoned.
		CHECK(Context.AiQueue[0] == 0);
	}
	SECTION("its gear HP's maximum is the same command's damage kind and flags")
	{
		FAiBattle Battle({W(0x80), W(0x06, 0, 0x34, 0x12), W(0x2F, 0, 0, 0), End, Stop});
		FBattleContext& Context = *Battle.Context;
		RunBattleTurnScript(Context, 3);
		REQUIRE(Context.Fault == EBattleFault::None);
		const FBattleCommand& Fourth = Context.Commands[FBattleContext::PartyCommandIndex(0, 3)];
		CHECK(Fourth.DamageKind == 0x2340);
		CHECK(Fourth.Flags == 0x0001);
	}
	SECTION("its status words lie in the result's still empty arrays")
	{
		FAiBattle Battle({W(0x80), W(0x05, 3, 0x11, 0x11), W(0x05, 4, 0x22, 0x22), W(0x2C, 3, 2, 0), W(0x2C, 4, 4, 0), End, Stop});
		FBattleContext& Context = *Battle.Context;
		RunBattleTurnScript(Context, 3);
		REQUIRE(Context.Fault == EBattleFault::None);
		const auto& Words = Context.AiRecords[0].Variables;
		CHECK((Words[0x10 + 6] | Words[0x10 + 7] << 8) == 0);
		CHECK((Words[0x10 + 8] | Words[0x10 + 9] << 8) == 0);
	}
	SECTION("a field the scripts do not reach still stops the battle")
	{
		FAiBattle Battle({W(0x80), W(0x2C, 3, 12, 0), End, Stop});
		RunBattleTurnScript(*Battle.Context, 3);
		CHECK(Battle.Context->Fault == EBattleFault::SlotOutsideBattle);
	}
}

TEST_CASE("A hidden enemy that shows itself keeps its place in its group, or in the original leaves it", "[Battle]")
{
	// Enemy slot 3, hidden in group 1 beside another enemy, shows itself (63 00).
	const auto Count = [](const FBattleRuleOptions& Options)
	{
		FAiBattle Battle({W(0x80), W(0x63, 0), End, Stop});
		FBattleContext& Context = *Battle.Context;
		Context.RuleOptions = Options;
		Context.Slots[3].Group = 1;
		Context.Slots[3].Place = 1;
		Context.Slots[3].Presence = 0x80;
		Context.Occupancy[8 + 1] = {2, 0x03};
		RunBattleTurnScript(Context, 3);
		REQUIRE(Context.Fault == EBattleFault::None);
		CHECK(Context.Slots[3].Presence == 0);
		return Context.Occupancy[8 + 1].Count;
	};
	CHECK(Count(FBattleRuleOptions()) == 2);
	CHECK(Count(FBattleRuleOptions::Original()) == 1);
}

TEST_CASE("Deus's second form takes the HP its first turn sets, or in the original keeps its record's", "[Battle]")
{
	// Scene 63's type 1 in slot 3: L0 = 0x124F * 16; slot(W0).gear HP = L0, with W0 never set.
	const auto Run = [](const FBattleRuleOptions& Options, uint8 Scene)
	{
		FAiBattle Battle({W(0x80), W(0x06, 0, 0x4F, 0x12), W(0x2F, 0, 1, 0), End, Stop});
		FBattleContext& Context = *Battle.Context;
		Context.RuleOptions = Options;
		Context.Formation.Scene = Scene;
		Context.Slots[3].Occupant = 1;
		Context.Combatants[3].GearPart.Hp = 70000;
		RunBattleTurnScript(Context, 3);
		REQUIRE(Context.Fault == EBattleFault::None);
		return std::pair{Context.Combatants[3].GearPart.Hp, Context.Commands[FBattleContext::PartyCommandIndex(0, 3)].Bytes04};
	};
	CHECK(Run(FBattleRuleOptions(), 63) == std::pair{uint32{74992}, uint32{0}});
	CHECK(Run(FBattleRuleOptions::Original(), 63) == std::pair{uint32{70000}, uint32{74992}});
	// Another scene's write through an empty mask stays the original's.
	CHECK(Run(FBattleRuleOptions(), 62) == std::pair{uint32{70000}, uint32{74992}});
}

TEST_CASE("A queue write's third byte keeps the battle camera with the fix, which marks Deus's attack on one gear", "[Battle]")
{
	SECTION("the byte as the scene's file gives it")
	{
		const auto Kept = [](const FBattleRuleOptions& Options, uint8 Third)
		{
			FAiBattle Battle({W(0x80), W(0x01, 1, 0x11, 0), W(0x01, 0, 1, Third), End, Stop});
			Battle.Context->RuleOptions = Options;
			RunBattleTurnScript(*Battle.Context, 3);
			REQUIRE(Battle.Context->Fault == EBattleFault::None);
			return Battle.Context->KeepCamera;
		};
		CHECK(Kept(FBattleRuleOptions(), 1) == 1);
		CHECK(Kept(FBattleRuleOptions(), 0) == 0);
		CHECK(Kept(FBattleRuleOptions::Original(), 1) == 0);
	}
	SECTION("Deus's word in his first fight, whose byte the disc leaves 0")
	{
		// Scene 64's type 0: the attack's commands, its entry committed by the word at C22h.
		constexpr uint32 Attack = 0xC22;
		const auto Kept = [](const FBattleRuleOptions& Options, uint8 Scene, uint32 At)
		{
			FAiBattle Battle({End});
			Battle.Scene.assign(At - 8, 0);
			Battle.Append({W(0x80), W(0x01, 1, 0x11, 0), W(0x01, 0, 1, 0), End, Stop}, 0);
			FBattleContext& Context = *Battle.Context;
			Context.SceneFile = Battle.Scene;
			Context.RuleOptions = Options;
			Context.Formation.Scene = Scene;
			RunBattleTurnScript(Context, 3);
			REQUIRE(Context.Fault == EBattleFault::None);
			REQUIRE(Context.AiQueue[1] == 0x11);
			return Context.KeepCamera;
		};
		CHECK(Kept(FBattleRuleOptions(), 64, Attack) == 1);
		CHECK(Kept(FBattleRuleOptions::Original(), 64, Attack) == 0);
		CHECK(Kept(FBattleRuleOptions(), 63, Attack) == 0);
		CHECK(Kept(FBattleRuleOptions(), 64, Attack + 4) == 0);
	}
}

// Perfect Works Build's rebalanced party's Battle reads the queue write's third byte as its bug-fix Battle does, with the fix's
// row or without it; the word that follows doesn't count.
TEST_CASE("A queue write's third byte keeps the battle camera under Perfect Works Build's rebalance", "[Battle]")
{
	const auto Kept = [](const FBattleRuleOptions& Options, uint8 Third)
	{
		FAiBattle Battle({W(0x80), W(0x01, 0, 1, Third), W(0x80), End, Stop});
		Battle.Context->RuleOptions = Options;
		Battle.Context->bPerfectWorksRebalance = true;
		RunBattleTurnScript(*Battle.Context, 3);
		REQUIRE(Battle.Context->Fault == EBattleFault::None);
		return Battle.Context->KeepCamera;
	};
	CHECK(Kept(FBattleRuleOptions::Original(), 1) == 1);
	CHECK(Kept(FBattleRuleOptions::Original(), 0) == 0);
	CHECK(Kept(FBattleRuleOptions(), 0) == 0);
}
