#include "Battle/BattleContext.h"
#include "Battle/BattleParty.h"
#include "Battle/BattleProgram.h"
#include "Game/GameState.h"
#include "Game/PsxRandom.h"
#include "Graphics/GteMath.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <initializer_list>
#include <memory>

using namespace Weltall;

namespace
{
	constexpr uint8 Billy = 4;
	constexpr uint8 AttackMode = 5;
	constexpr uint8 FadingAttackMode = 100;
	/** Where the game state keeps Billy's ammunition counts, by kind. */
	constexpr std::size_t AmmunitionCounts = 0x2286;

	/** A party member (slot 0) against one enemy (slot 3) in reach, with the Battle program's own tables. */
	struct FPartyDuel
	{
		FGameState State;
		FPsxRandom Random{11};
		std::unique_ptr<FBattleContext> Context = std::make_unique<FBattleContext>();

		FPartyDuel(const FBattleProgramTables& Program, const FGteArctangents& Arctangents, uint8 Character)
		{
			FBattleContext& C = *Context;
			C.GameState = &State;
			C.Random = &Random;
			C.Program = &Program;
			C.Arctangents = &Arctangents;
			for (const uint8 Slot : {uint8{0}, uint8{3}})
			{
				C.InBattle[Slot] = true;
				FBattleCombatant& Combatant = C.Combatants[Slot];
				Combatant.MaxHp = 900;
				Combatant.Hp = 900;
				Combatant.Hit = 90;
			}
			C.Combatants[0].Character = Character;
			C.PartyCharacters = {Character, FBattleContext::NoCharacter, FBattleContext::NoCharacter};
			C.ApRecords[0].ApPerTurn = 3;
			C.SlotTargets[0] = 3;
			C.TurnActor = 0;
			// Every basic attack a plain hit, Billy's guns loaded (his ammunition kinds 0 and their counts).
			for (int32 Command = 0; Command < 7; ++Command)
			{
				C.Commands[FBattleContext::PartyCommandIndex(0, Command)].Power = 5;
			}
			State.Block()[AmmunitionCounts] = 9;
		}

		/** One frame with the show acting as the oracle's (the turn icon arrives at once). */
		bool Step(uint8 Press)
		{
			if (Context->Party.TurnIcons[0] == 3)
			{
				Context->Party.TurnIcons[0] = 1;
			}
			return StepBattlePartyTurn(*Context, TakeBattleMenuPress(*Context, Press));
		}

		void Steps(std::initializer_list<uint8> Presses)
		{
			for (const uint8 Press : Presses)
			{
				Step(Press);
			}
		}
	};

	struct FProgram
	{
		TResult<FBattleProgramTables> Tables = MakeError("not read");
		TResult<FGteArctangents> Arctangents = MakeError("not read");
	};

	FProgram ReadProgram(const FIso9660& Disc)
	{
		FProgram Program;
		const auto Executable = ReadBootExecutable(Disc);
		REQUIRE(Executable);
		const auto Index = FGameFileIndex::Read(*Executable);
		REQUIRE(Index);
		Program.Tables = FBattleProgramTables::Load(Disc.Image(), *Index);
		Program.Arctangents = FGteArctangents::Read(*Executable);
		REQUIRE(Program.Tables);
		REQUIRE(Program.Arctangents);
		return Program;
	}
}

TEST_CASE("A press in the frames Billy's ammunition window takes to close after a cancel still attacks, as in the game", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FProgram Program = ReadProgram(*Disc);
	FPartyDuel Duel(*Program.Tables, *Program.Arctangents, Billy);
	FBattleContext& Context = *Duel.Context;
	BeginBattlePartyTurn(Context, 0);
	// The icon's frame, the menu's first frame, then Attack confirmed; the ammunition window opens in the first
	// frame of the attack mode.
	Duel.Steps({BattleMenuEvents::Nothing, BattleMenuEvents::Confirm, BattleMenuEvents::Nothing, BattleMenuEvents::Nothing});
	REQUIRE(Context.Fault == EBattleFault::None);
	REQUIRE(Context.Party.MenuState == AttackMode);
	REQUIRE(Context.Party.bAmmunitionShown);
	// Cancel before any hit: back to the menu, the window closing over two frames; the second frame's Weak is taken.
	Duel.Steps({BattleMenuEvents::Cancel, BattleMenuEvents::Nothing, BattleMenuEvents::Weak});
	REQUIRE(Context.Fault == EBattleFault::None);
	CHECK(Context.Party.MenuState == FadingAttackMode);
	CHECK(Context.Party.ApLeft == 2);
	CHECK(Context.Party.bTargetLocked);
	CHECK_FALSE(Context.Party.bAmmunitionShown);
}

TEST_CASE("The party's turns stop where the game would loop forever or place by a register it never set", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FProgram Program = ReadProgram(*Disc);
	SECTION("a confused member with no one but itself in reach")
	{
		FPartyDuel Duel(*Program.Tables, *Program.Arctangents, 0);
		FBattleContext& Context = *Duel.Context;
		Context.Combatants[0].Mental = 0x2000;
		Context.InBattle[3] = false;
		// Draws past the board and ability rolls into an attack.
		for (int32 Attempt = 0; Attempt < 64 && Context.Fault == EBattleFault::None; ++Attempt)
		{
			Context.Party.Depth = 0;
			BeginBattleAutoTurn(Context, 0);
			Duel.Steps({BattleMenuEvents::Nothing, BattleMenuEvents::Nothing, BattleMenuEvents::Nothing});
		}
		CHECK(Context.Fault == EBattleFault::NoOtherTarget);
	}
	SECTION("boarding when every party gear place is taken")
	{
		FPartyDuel Duel(*Program.Tables, *Program.Arctangents, 0);
		FBattleContext& Context = *Duel.Context;
		for (int32 Group = 0; Group < FBattleStage::GroupCount; ++Group)
		{
			Context.Occupancy[16 + Group].Count = 1;
		}
		BeginBattlePartyTurn(Context, 0);
		// The icon's frame, then Up twice (Ether, then from it the Gear command) and confirm.
		Duel.Steps({BattleMenuEvents::Nothing, BattleMenuEvents::Up, BattleMenuEvents::Up, BattleMenuEvents::Confirm});
		CHECK(Context.Fault == EBattleFault::NoGearGroup);
	}
}
