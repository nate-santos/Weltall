#include "Input/PadLatch.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

namespace
{
	FPadState Holding(std::initializer_list<EPadButton> Buttons)
	{
		FPadState Pad;
		for (const EPadButton Button : Buttons)
		{
			Pad.Buttons |= FPadState::Bit(Button);
		}
		return Pad;
	}
}

TEST_CASE("A press shorter than a tick reaches the next tick once", "[Input]")
{
	FPadLatch Latch;
	Latch.Sample(Holding({EPadButton::Circle}));
	Latch.Sample(Holding({}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Circle}));
	CHECK(Latch.TakeForTick() == Holding({}));
}

TEST_CASE("A held button stays held and a released one is gone at the next tick", "[Input]")
{
	FPadLatch Latch;
	Latch.Sample(Holding({EPadButton::Up, EPadButton::Square}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Up, EPadButton::Square}));
	Latch.Sample(Holding({EPadButton::Up, EPadButton::Square}));
	Latch.Sample(Holding({EPadButton::Up}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Up}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Up}));
}

TEST_CASE("A button pressed again between ticks is pressed again", "[Input]")
{
	FPadLatch Latch;
	Latch.Sample(Holding({EPadButton::Circle}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Circle}));
	Latch.Sample(Holding({}));
	Latch.Sample(Holding({EPadButton::Circle}));
	Latch.Sample(Holding({}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Circle}));
	CHECK(Latch.TakeForTick() == Holding({}));
}

TEST_CASE("Several ticks in one frame see a short press only once", "[Input]")
{
	FPadLatch Latch;
	Latch.Sample(Holding({EPadButton::Cross, EPadButton::Left}));
	Latch.Sample(Holding({EPadButton::Left}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Cross, EPadButton::Left}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Left}));
	CHECK(Latch.TakeForTick() == Holding({EPadButton::Left}));
}

TEST_CASE("The stick reaches the tick where the latest frame found it, its directions its own while nothing else holds them", "[Input]")
{
	FPadLatch Latch;
	FPadState Tilted = Holding({EPadButton::Right});
	Tilted.StickButtons = FPadState::Bit(EPadButton::Right);
	Tilted.StickX = 0xE0;
	Tilted.StickY = 0x70;
	Tilted.bAnalog = true;
	Latch.Sample(Tilted);
	FPadState Back = Holding({});
	Back.bAnalog = true;
	Back.StickX = 0x90;
	Latch.Sample(Back);
	const FPadState Taken = Latch.TakeForTick();
	CHECK(Taken.Buttons == FPadState::Bit(EPadButton::Right));
	CHECK(Taken.StickButtons == FPadState::Bit(EPadButton::Right));
	CHECK(Taken.StickX == 0x90);
	CHECK(Taken.StickY == FPadState::StickCentre);
	CHECK(Taken.bAnalog);

	// The d-pad holds Right too: the direction is no longer the stick's alone.
	FPadState Both = Tilted;
	Latch.Sample(Both);
	Both.StickButtons = 0;
	Latch.Sample(Both);
	CHECK(Latch.TakeForTick().StickButtons == 0);
}
