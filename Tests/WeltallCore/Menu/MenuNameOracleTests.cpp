#include "Game/GameState.h"
#include "Support/GameDiscs.h"
#include "Support/MenuOracleRun.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;
using namespace Weltall::Testing;
using namespace Weltall::Testing::MenuPad;

// Enter Name against the original module run whole under the oracle harness,
// frame by frame.

TEST_CASE("Enter Name types a name over the old one on its grid, as the original does", "[Content][Menu]")
{
	// Fei's name: the cursor from "End" left and up, characters typed, a spacer passed, a row wrapped, backspaces, the
	// insertion place moved by R1 and L1, a blank cell typed, typing past the length (the buzzer), Start.
	FMenuScenario Scenario{"name_fei", EMenuMode::EnterName, 0,
		{{0, 60}, {Left, 2}, {0, 4}, {Left, 2}, {0, 4}, {Left, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Up, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Right, 2}, {0, 4},
			{Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Down, 2}, {0, 4},
			{Down, 2}, {0, 4}, {Down, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Circle, 2}, {0, 6}, {Circle, 2}, {0, 6}, {Cross, 2}, {0, 6}, {R1, 2}, {0, 4}, {R1, 2},
			{0, 4}, {Cross, 2}, {0, 6}, {L1, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Down, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4}, {Right, 2}, {0, 4},
			{Cross, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Cross, 2}, {0, 6},
			{Up, 2}, {0, 40}, {Start, 2}, {0, 60}},
		900};
	CheckMenuScenario(Scenario, "69b2f12b",
		"cb130f8e cf1ba3eb ea52a891 2e11a7db 80a1b6cc a2170b1d e524b770 eb551318 87c59dfa 09725938 5b4f0cf0 f98c5ea4 f1058fa4 06b92f75 "
		"127127cb 53c8eb45 3ca9dfd2 94aad4f7 ec5f33eb babc9a5e 38abdeeb ee10a363 82418560 8d149ad2 d7e8410d 384a3e0b 819e7b65 22def8b1 "
		"a42f9014 fc5ef0d9 c3a739c2 5529b222 b6db6428 0958d035 7f53a4d4 4c725838 3d6692c4 aede4306 2404a4da ca74b140 46796727 f5d168ee "
		"4b58d473 d937e5ef*18 291d4c99 45a97ac4*5 d07ac1d0 add05c07*4 474f4d36 0669efcd 9c732639*6 0067b069 9c732639*6 f818807b 433bc200*6 "
		"9e1a1a62 433bc200*6 6d477ac3 9ce4a16f 6cd01781*4 bbe9e398 110cfcb3*5 1389c4b3 86e6424c*5 69af4079 39645f9a*5 5bfd48c2 02743d4e*5 "
		"ef08b467 e10ef85a cc67ad3f*5 ade4f58d cc67ad3f*6 a8b09772 db6e1b25*5 f8f4279c 45d1a9d6*5 41154f28 615d0ce5*6 e2643b7a c4cd9dc7*6 "
		"fb8a9644 508e1c67 eeb95135*6 2d337ec7 1323ca2b 0fc8dccc*7 948c414a eeb95135*5 615d0ce5*14 f4d548f2 615d0ce5*13 f4d548f2 615d0ce5*2 "
		"b06aa5d8*4 46cc8b9d 0dc9c2ae*5 ef0397f6 6d300885*5 9d939188 60eb1a16*5 21fbfc5f 5c5c19c2*6 740626cb d48748ad*7 1536011e d48748ad*7 "
		"1536011e d48748ad*7 34187b28 d48748ad*7 252f22d9 1cc4e8c2*7 252f22d9 1cc4e8c2*7 252f22d9 1cc4e8c2*6 d8809d9d 45a8b5aa*6 5f5cbac6*31 "
		"45a8b5aa*5 54c321ca b467b23f 0777b2d7 186d1148 974593bc 61d98b6d 7258170b 0eee96b4 9faffe9f 6a21ef0e a29448cd 2c6e7ae6 521c525e "
		"2dd8962a 7956e63f 81d0b135 d3a3252a 8a50d85b ad7c6a58 24ddaa35*5");
}

TEST_CASE("Enter Name refuses a name whose first glyph is deleted, as the original does", "[Content][Menu]")
{
	// Weltall's name: its first glyph deleted, Start and "End" refused, then typed and ended on "End".
	FMenuScenario Scenario{"name_weltall", EMenuMode::EnterName, 11,
		{{0, 60}, {Circle, 2}, {0, 6}, {Start, 2}, {0, 6}, {Cross, 2}, {0, 6}, {Up, 2}, {0, 4}, {Cross, 2}, {0, 6}, {Down, 2}, {0, 6}, {Cross, 2}, {0, 60}},
		500};
	CheckMenuScenario(Scenario, "f7ff0b2f",
		"cb130f8e cf1ba3eb f7cfabfd 114bff70 9974edbd 7c1683ed 7dfc5a2b 383c8cae e11f7dd4 dc185eba 2663fca5 28f67308 85901280 71a6bace "
		"f799bebc 6783bf90 381c0cbc 83935bf4 347788c6 a991e1ff f60dd347 c8502255 180a30f5 dc504f5a 9b0fc531 748ef6bf b53eded4 cc30d721 "
		"56fadc38 44e0f2f7 593eba9e 1e391eae 8377ee9b a0750b85 d50d86a2 6cead99a 0567ba55 eaa2bce1 29ce3bb6 decd8c60 92e2caab 89903890 "
		"de043f60 c5523118*18 ded951ba 4bd481f5 c5523118*9 e3157dc0*13 8ca9d94c 55cfb37f*6 f6232872 55cfb37f*6 ec0beef2 e3157dc0*3 4e2ca113*5 "
		"498d38b9 b800e92a 8f715c8a 82da135b 7c2372c9 fea75e8c 067fd4eb 5cc155bb 5bfbc416 7f6d725c 66ed67c4 0cd6b70f 53c10818 c62b4a80 "
		"ad6a531f 0e700fea 38125098 db166965 ad7c6a58 24ddaa35*5");
}

TEST_CASE("Enter Name cuts a confirmed name's trailing s, as the original does, and keeps it with the fix", "[Content][Menu]")
{
	// Crescens' name confirmed as it is, the grown Emeralda's portrait shown.
	FMenuScenario Scenario{"name_crescens", EMenuMode::EnterName, 20,
		{{0, 70}, {Start, 2}, {0, 60}},
		300};
	Scenario.Pokes = {
		{0x1d30, {0xff, 0x7, 0xff, 0x7}},
	};
	CheckMenuScenario(Scenario, "d6d62569",
		"dfb46062 cf1ba3eb ad4fc403 114bff70 9974edbd 7c1683ed 7dfc5a2b 383c8cae e11f7dd4 dc185eba 2663fca5 28f67308 85901280 71a6bace "
		"f799bebc 6783bf90 381c0cbc 83935bf4 347788c6 a991e1ff f60dd347 c8502255 180a30f5 dc504f5a 9b0fc531 748ef6bf b53eded4 cc30d721 "
		"56fadc38 44e0f2f7 593eba9e 1e391eae 8377ee9b a0750b85 d50d86a2 6cead99a 0567ba55 eaa2bce1 29ce3bb6 decd8c60 92e2caab 89903890 "
		"de043f60 c5523118*29 66a5469c b800e92a 8f715c8a 82da135b 7c2372c9 fea75e8c 067fd4eb 5cc155bb 5bfbc416 7f6d725c 66ed67c4 0cd6b70f "
		"53c10818 c62b4a80 ad6a531f 0e700fea 38125098 db166965 ad7c6a58 24ddaa35*5");

	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMenuRun Original = RunMenuScenario(*Disc, Scenario);
	Scenario.Options = FMenuOptions();
	const FMenuRun Fixed = RunMenuScenario(*Disc, Scenario);
	int32 Changed = 0;
	for (int32 Slot = 0; Slot < 40; ++Slot)
	{
		const std::span<const uint8> Cut = Original.Game.Name(Slot);
		const std::span<const uint8> Kept = Fixed.Game.Name(Slot);
		if (std::equal(Cut.begin(), Cut.end(), Kept.begin(), Kept.end()))
		{
			continue;
		}
		++Changed;
		REQUIRE(Kept.size() == Cut.size() + 1);
		CHECK(std::equal(Cut.begin(), Cut.end(), Kept.begin()));
		CHECK(Kept.back() == 0x4F);
	}
	CHECK(Changed > 0);
}

TEST_CASE("Enter Name keeps a confirmed name's trailing blanks, as the original does, and trims them with the fix", "[Content][Menu]")
{
	// Fei's name with two blanks after it, confirmed as it is.
	constexpr uint8 Blank = 0x10;
	FMenuScenario Scenario{"name_blanks", EMenuMode::EnterName, 0, {{0, 70}, {Start, 2}, {0, 60}}, 300};
	Scenario.Pokes = {{3, {Blank, Blank}}};

	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMenuRun Original = RunMenuScenario(*Disc, Scenario);
	Scenario.Options = FMenuOptions();
	const FMenuRun Fixed = RunMenuScenario(*Disc, Scenario);
	const std::span<const uint8> Kept = Original.Game.Name(0);
	const std::span<const uint8> Trimmed = Fixed.Game.Name(0);
	REQUIRE(Kept.size() == 5);
	CHECK(Kept[3] == Blank);
	CHECK(Kept[4] == Blank);
	REQUIRE(Trimmed.size() == 3);
	CHECK(std::equal(Trimmed.begin(), Trimmed.end(), Kept.begin()));
}
