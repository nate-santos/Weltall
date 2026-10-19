#include "Field/EventContext.h"
#include "Field/FieldControls.h"
#include "Game/GameBalance.h"
#include "Game/PsxRandom.h"
#include "WorldMap/WorldMapEncounters.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <limits>

using namespace Weltall;

namespace
{
	constexpr std::array<EEncounterRate, 5> EncounterRates = {EEncounterRate::None, EEncounterRate::Quarter, EEncounterRate::Half,
		EEncounterRate::Original, EEncounterRate::Double};

	/** The battles a field of Period ticks and PerPeriod encounters starts in Ticks of the leader's steps at Rate. */
	int32 FieldBattles(EEncounterRate Rate, int32 Period, int32 PerPeriod, int32 Ticks)
	{
		FEventContext Context;
		FPsxRandom Random(0x1234);
		Context.Random = &Random;
		Context.bFieldRunning = true;
		Context.EncounterRate = Rate;
		Context.Encounters.Period = Period;
		Context.Encounters.PerPeriod = PerPeriod;
		Context.Encounters.Weights.fill(0xFF);
		REQUIRE(Context.Encounters.Schedule(Random, Rate));
		int32 Battles = 0;
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			REQUIRE(RollFieldEncounter(Context));
			if (Context.bBattleRequested)
			{
				++Battles;
				Context.bBattleRequested = false;
			}
		}
		return Battles;
	}

	/** The world map's encounters in Ticks moving ticks on foot at Rate. */
	int32 WorldMapBattles(EEncounterRate Rate, int32 Ticks)
	{
		FWorldMapEncounters Encounters;
		FPsxRandom Random(0x4321);
		Encounters.Reset(false);
		Encounters.Rate = Rate;
		int32 Battles = 0;
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			Encounters.Tick(Random);
			Battles += Encounters.Fired;
		}
		return Battles;
	}
}

TEST_CASE("Rewards are multiplied and rounded down, and stay within 32 bits", "[Game][Balance]")
{
	CHECK(RewardAt(9, ERewardRate::Original) == 9);
	CHECK(RewardAt(9, ERewardRate::OneAndAHalf) == 13);
	CHECK(RewardAt(9, ERewardRate::Double) == 18);
	CHECK(RewardAt(9, ERewardRate::Triple) == 27);
	CHECK(RewardAt(9, ERewardRate::Quadruple) == 36);
	// The largest boss's experience, past the 16 bits a stored reward would have.
	CHECK(RewardAt(189465, ERewardRate::Double) == 378930);
	CHECK(RewardAt(std::numeric_limits<uint32>::max() / 2, ERewardRate::Quadruple) == std::numeric_limits<uint32>::max());
	CHECK(RewardAt(0, ERewardRate::Quadruple) == 0);
}

TEST_CASE("An encounter period lengthens for fewer battles and shortens for more", "[Game][Balance]")
{
	CHECK(EncounterPeriodAt(600, EEncounterRate::Original) == 600);
	CHECK(EncounterPeriodAt(600, EEncounterRate::None) == 600);
	CHECK(EncounterPeriodAt(600, EEncounterRate::Half) == 1200);
	CHECK(EncounterPeriodAt(600, EEncounterRate::Quarter) == 2400);
	CHECK(EncounterPeriodAt(600, EEncounterRate::Double) == 300);
	CHECK(EncounterPeriodAt(1, EEncounterRate::Double) == 1);
}

TEST_CASE("The field's schedule spans the period as the rate makes it, within what the schedule can hold", "[Field][Balance]")
{
	const auto Scheduled = [](int32 Period, int32 PerPeriod, EEncounterRate Rate)
	{
		FFieldEncounters Encounters;
		FPsxRandom Random(7);
		Encounters.Period = Period;
		Encounters.PerPeriod = PerPeriod;
		REQUIRE(Encounters.Schedule(Random, Rate));
		for (int32 Index = 0; Index < PerPeriod; ++Index)
		{
			const uint16 Due = Encounters.Pending[static_cast<std::size_t>(Index)];
			CHECK(Due >= 1);
			CHECK(Due <= Encounters.Countdown + 1);
		}
		CHECK(Encounters.Period == Period);
		return Encounters.Countdown;
	};
	CHECK(Scheduled(600, 1, EEncounterRate::Original) == 600);
	CHECK(Scheduled(600, 1, EEncounterRate::None) == 600);
	CHECK(Scheduled(600, 1, EEncounterRate::Half) == 1200);
	CHECK(Scheduled(600, 1, EEncounterRate::Quarter) == 2400);
	CHECK(Scheduled(600, 1, EEncounterRate::Double) == 300);
	// Never past the ticks a pending encounter can count, nor too short for the period's encounters to differ.
	CHECK(Scheduled(0x4000, 1, EEncounterRate::Quarter) == FFieldEncounters::NoEncounter - 2);
	CHECK(Scheduled(40, 32, EEncounterRate::Double) == 31);
	CHECK(Scheduled(2, 1, EEncounterRate::Double) == 1);

	// The original's draws are kept as they were at its own rate.
	FFieldEncounters Original;
	FFieldEncounters Kept;
	FPsxRandom First(99);
	FPsxRandom Second(99);
	Original.Period = Kept.Period = 300;
	Original.PerPeriod = Kept.PerPeriod = 3;
	REQUIRE(Original.Schedule(First, EEncounterRate::Original));
	REQUIRE(Kept.Schedule(Second, EEncounterRate::None));
	CHECK(Original.Pending == Kept.Pending);
	CHECK(First.Seed() == Second.Seed());
}

TEST_CASE("The field's battles come at the player's rate, and none come at None", "[Field][Balance]")
{
	constexpr int32 Ticks = 20000;
	const int32 Original = FieldBattles(EEncounterRate::Original, 200, 2, Ticks);
	CHECK(Original > 150);
	CHECK(FieldBattles(EEncounterRate::None, 200, 2, Ticks) == 0);
	CHECK(std::abs(FieldBattles(EEncounterRate::Half, 200, 2, Ticks) * 2 - Original) <= Original / 10);
	CHECK(std::abs(FieldBattles(EEncounterRate::Quarter, 200, 2, Ticks) * 4 - Original) <= Original / 10);
	CHECK(std::abs(FieldBattles(EEncounterRate::Double, 200, 2, Ticks) - Original * 2) <= Original / 5);
}

TEST_CASE("The world map gives one battle a period, its period stretched or shrunk by the rate", "[WorldMap][Balance]")
{
	// On foot a period is 384 moving ticks; 7,680 ticks hold exactly 20 of them.
	constexpr int32 Ticks = 384 * 20;
	CHECK(WorldMapBattles(EEncounterRate::Original, Ticks) == 20);
	CHECK(WorldMapBattles(EEncounterRate::Half, Ticks) == 10);
	CHECK(WorldMapBattles(EEncounterRate::Quarter, Ticks) == 5);
	CHECK(WorldMapBattles(EEncounterRate::Double, Ticks) == 40);
	CHECK(WorldMapBattles(EEncounterRate::None, Ticks) == 0);

	// None leaves the schedule where it stood: the next tick at another rate goes on from it.
	FWorldMapEncounters Encounters;
	FPsxRandom Random(5);
	Encounters.Reset(false);
	Encounters.Tick(Random);
	const int32 Left = Encounters.Left;
	Encounters.Rate = EEncounterRate::None;
	Encounters.Tick(Random);
	CHECK(Encounters.Left == Left);
	CHECK(Encounters.Fired == 0);
	for (const EEncounterRate Rate : EncounterRates)
	{
		INFO(static_cast<int32>(Rate));
		CHECK(WorldMapBattles(Rate, 0) == 0);
	}
}
