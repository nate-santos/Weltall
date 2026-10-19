#include "Battle/Show/BattleShowFrame.h"
#include "Battle/Show/BattleShowScreen.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

// The Battle flashes setting draws an effect's adding fades, its flashes, whole, at a quarter or not at all; the battle's own
// fades and an effect's subtracting ones as they are.
TEST_CASE("An effect's flashes are drawn whole, at a quarter or not at all, and every other fade as it is", "[Battle]")
{
	constexpr uint8 Half = 0, Add = 1, Subtract = 2, AddQuarter = 3;
	const auto Share = [](uint8 Mode, bool bFromEffect, EBattleFlashes Flashes)
	{ return DrawnFadeShare(FBattleShowFade{Mode, {255, 204, 136}, bFromEffect}, Flashes); };
	for (const uint8 Mode : {Add, AddQuarter})
	{
		CHECK(Share(Mode, true, EBattleFlashes::Original) == 1.0f);
		CHECK(Share(Mode, true, EBattleFlashes::Softer) == 0.25f);
		CHECK(Share(Mode, true, EBattleFlashes::Off) == 0.0f);
		CHECK(Share(Mode, false, EBattleFlashes::Off) == 1.0f);
	}
	for (const uint8 Mode : {Half, Subtract})
	{
		CHECK(Share(Mode, true, EBattleFlashes::Off) == 1.0f);
		CHECK(Share(Mode, true, EBattleFlashes::Softer) == 1.0f);
	}
}

TEST_CASE("A fade keeps whether an effect started it until the next start, which a block refuses", "[Battle]")
{
	FBattleFade Fade;
	Fade.Start(0, 1, 255, 204, 136, true);
	CHECK(Fade.bFromEffect);
	// The flash's colour itself is the game's: the setting changes only how much of it is drawn.
	CHECK(Fade.Shown == std::array<uint8, 3>{255, 204, 136});
	Fade.Start(8, 2, 255, 255, 255);
	CHECK_FALSE(Fade.bFromEffect);
	Fade.bBlocked = true;
	Fade.Start(0, 1, 255, 255, 255, true);
	CHECK_FALSE(Fade.bFromEffect);
	CHECK(Fade.Mode == 2);
}
