#include "Support/GameDiscs.h"
#include "Support/MenuOracleRun.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;
using namespace Weltall::Testing;
using namespace Weltall::Testing::MenuPad;

// Member Change against the original module run whole under the oracle harness,
// frame by frame.

TEST_CASE("Member Change swaps members between the party and the reserve, as the original does", "[Content][Menu]")
{
	// The party column's rows and wrap, the reserve's clamp, Rico picked and swapped with Citan, a pick dropped with
	// Cancel, Fei refused against Emeralda (locked) then swapped with Citan, the empty slot refused, out.
	FMenuScenario Scenario{"member_change", EMenuMode::MemberChange, 0,
		{{0, 20}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Up, 2}, {0, 4}, {Right, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4},
			{Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Up, 2}, {0, 4}, {Up, 2}, {0, 4}, {Cross, 2},
			{0, 6}, {Down, 2}, {0, 4}, {Cross, 2}, {0, 12}, {Cross, 2}, {0, 6}, {Circle, 2}, {0, 6}, {Left, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Down, 2}, {0, 4},
			{Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Up, 2}, {0, 4}, {Cross, 2}, {0, 12}, {Left, 2}, {0, 4}, {Down, 2},
			{0, 4}, {Down, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Circle, 2}, {0, 6}, {Circle, 2}, {0, 40}},
		500};
	Scenario.Pokes = {
		{0x1d30, {0xff, 0x7, 0xff, 0x7}},
		{0x2318, {0x40, 0x0}},
	};
	CheckMenuScenario(Scenario, "e406553e",
		"3966d157 76df94bb 2add275c 06258768 e0895cb8 5edc5e1f 182c6704*2 bbece1a8 0d3a586a 7f85a4cb 1e57e3fe a8ebb1f5 22c8c178 653ba6da*8 "
		"adee9177 b2e6d1ad*5 bdad70f9 3d617426*5 810bc505 653ba6da*5 adee9177 3d617426*5 810bc505 388a7bba*5 ce427fe0 2b5bd904*5 d3fa7aa8 "
		"385a1aa0*5 96a656f3 2952c8ab*5 7301570d 87630223*5 65bfac80 87b30c50*5 b0777ee7 87b30c50*5 b0777ee7 87b30c50*5 b0777ee7 87630223*5 "
		"65bfac80 2952c8ab*5 671557ff d43682b1*7 ca3e07be 9a8cc02e*5 c20d32e2 4d2623b3*4 5f2d03fd*9 df9ba499 e63a6f16*7 a7c0dd3b bdd646da*7 "
		"0af71f59 25f6fee9*5 f97f1b73 b83de426*7 0aec7945 e0242c8f*5 87144d4e e0092db5*5 de5afa64 e3b01acc*5 769640a0 7a93bb9a*5 9a5ac419 "
		"7a93bb9a*7 3a9e0332 e3b01acc*5 2427be04 89cef580*4 9de544f7*9 e8e9975e 47dd95f7*5 62c3facc 4cb72356*5 7d95ddd5 144902b9*5 0338141e "
		"2167564f*7 1158fa51 2167564f*7 d1112b30 79e4290f*7 fde8feed 5b117162*2 24ddaa35*3");
}

TEST_CASE("Member Change labels the EP as every other screen does with the fix, MP as the original does", "[Content][Menu]")
{
	FMenuScenario Scenario{"member_ep", EMenuMode::MemberChange, 0, {{0, 30}, {Circle, 2}, {0, 40}}, 100};
	Scenario.Pokes = {{0x1d30, {0xff, 0x7, 0xff, 0x7}}};
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMenuRun Original = RunMenuScenario(*Disc, Scenario);
	Scenario.Options.bMemberChangeSaysEp = true;
	const FMenuRun Fixed = RunMenuScenario(*Disc, Scenario);
	REQUIRE(Original.Frames.size() == Fixed.Frames.size());
	CHECK(Original.Frames.front() == Fixed.Frames.front());
	CHECK(Original.Frames != Fixed.Frames);
	CHECK(Original.Leaves == Fixed.Leaves);
}
