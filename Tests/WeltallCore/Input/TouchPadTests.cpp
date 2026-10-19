#include "Input/TouchPad.h"
#include "Input/PadLatch.h"
#include "Input/KernelPad.h"
#include "Game/PlayerSettings.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <limits>
#include <locale>
#include <string>

using namespace Weltall;

namespace
{
	FTouchPoint Center(const FTouchLayout& Layout, ETouchControl Control)
	{
		return Layout.Controls[static_cast<std::size_t>(Control)].Center();
	}
}

TEST_CASE("Touch buttons preserve a controller's analog report through the kernel", "[Input][Touch]")
{
	const uint16 Right = FPadState::Bit(EPadButton::Right);
	FPadState Hardware{static_cast<uint16>(Right | FPadState::Bit(EPadButton::Cross))};
	Hardware.StickButtons = Right;
	Hardware.StickX = 240;
	Hardware.StickY = 24;
	Hardware.bAnalog = true;
	CHECK(MergePads(Hardware, {}) == Hardware);

	const FPadState Touch{static_cast<uint16>(Right | FPadState::Bit(EPadButton::Circle))};
	const FPadState Combined = MergePads(MergePads(Hardware, Touch), {});
	FKernelPad Kernel;
	Kernel.Vblank(Combined, 1);
	const FKernelPadWords Words = Kernel.TakeMerged();
	CHECK(Words.bAnalog);
	CHECK(Words.StickX == Hardware.StickX);
	CHECK(Words.StickY == Hardware.StickY);
	CHECK(Words.StickHeld == 0); // Right is also held by the touch D-pad.
	CHECK(Words.Held == FKernelPad::LogicalButtons(FKernelPad::ReportWord(FPadState{static_cast<uint16>(Hardware.Buttons | Touch.Buttons)})));
	CHECK(MergePads({}, Touch) == Touch);

	const FPadState ActionOnly{FPadState::Bit(EPadButton::Square)};
	CHECK(MergePads(Hardware, ActionOnly).StickButtons == Right);
}

TEST_CASE("Touch movement and two action fingers remain independent", "[Input][Touch]")
{
	const FTouchLayout Layout = FTouchLayout::Build(1600.0f, 720.0f, {});
	FTouchPad Pad;
	Pad.SetLayout(Layout);
	FTouchPoint Direction = Center(Layout, ETouchControl::DPad);
	Direction.X += 60.0f;
	Direction.Y -= 60.0f;
	REQUIRE(Pad.Begin(0, Direction));
	REQUIRE(Pad.Begin(4, Center(Layout, ETouchControl::Cross)));
	REQUIRE(Pad.Begin(9, Center(Layout, ETouchControl::Square)));
	CHECK(Pad.Held().IsHeld(EPadButton::Up));
	CHECK(Pad.Held().IsHeld(EPadButton::Right));
	CHECK(Pad.Held().IsHeld(EPadButton::Cross));
	CHECK(Pad.Held().IsHeld(EPadButton::Square));
	CHECK(Pad.End(4));
	CHECK_FALSE(Pad.Held().IsHeld(EPadButton::Cross));
	CHECK(Pad.Held().IsHeld(EPadButton::Square));
	CHECK(Pad.Held().IsHeld(EPadButton::Right));
	Pad.Reset();
	CHECK(Pad.Held().Buttons == 0);
	CHECK_FALSE(Pad.Move(9, Direction));
}

TEST_CASE("Repeated button touches and hardware holds do not release each other", "[Input][Touch]")
{
	const FTouchLayout Layout = FTouchLayout::Build(1280.0f, 720.0f, {});
	FTouchPad Pad;
	Pad.SetLayout(Layout);
	REQUIRE(Pad.Begin(2, Center(Layout, ETouchControl::Circle)));
	REQUIRE(Pad.Begin(3, Center(Layout, ETouchControl::Circle)));
	Pad.End(2);
	CHECK(Pad.Held().IsHeld(EPadButton::Circle));
	Pad.End(3);
	const FPadState Hardware{FPadState::Bit(EPadButton::Circle)};
	CHECK(MergePads(Hardware, Pad.Held()).IsHeld(EPadButton::Circle));
	CHECK_FALSE(MergePads({}, Pad.Held()).IsHeld(EPadButton::Circle));
}

TEST_CASE("Short touch taps survive until sampled while capture cancellation preserves other fingers", "[Input][Touch]")
{
	const FTouchLayout Layout = FTouchLayout::Build(1600.0f, 720.0f, {});
	FTouchPad Pad;
	Pad.SetLayout(Layout);
	for (const auto Control : {ETouchControl::Cross, ETouchControl::Circle, ETouchControl::Square, ETouchControl::Triangle,
		ETouchControl::L1, ETouchControl::R1, ETouchControl::L2, ETouchControl::R2, ETouchControl::Start, ETouchControl::Select, ETouchControl::L3, ETouchControl::R3})
	{
		REQUIRE(Pad.Begin(0, Center(Layout, Control)));
		const FPadState Pressed = Pad.Held();
		REQUIRE(Pressed.Buttons != 0);
		REQUIRE(Pad.End(0));
		CHECK_FALSE(Pad.Cancel(0)); // Slate's normal release-capture callback follows End.
		CHECK(Pad.Held().Buttons == 0);
		CHECK(Pad.TakeForTick() == Pressed);
		CHECK(Pad.TakeForTick().Buttons == 0);
	}
	FTouchPoint Left = Center(Layout, ETouchControl::DPad);
	Left.X -= 60.0f;
	REQUIRE(Pad.Begin(0, Left));
	REQUIRE(Pad.Begin(1, Center(Layout, ETouchControl::Cross)));
	REQUIRE(Pad.End(1));
	REQUIRE(Pad.Begin(2, Center(Layout, ETouchControl::Square)));
	REQUIRE(Pad.Cancel(2));
	const auto Mixed = Pad.TakeForTick();
	CHECK(Mixed.IsHeld(EPadButton::Left));
	CHECK(Mixed.IsHeld(EPadButton::Cross));
	CHECK_FALSE(Mixed.IsHeld(EPadButton::Square));
	CHECK(Pad.TakeForTick() == Pad.Held());
	REQUIRE(Pad.End(0));
	CHECK(Pad.TakeForTick().Buttons == 0);
	FTouchPoint Up = Center(Layout, ETouchControl::DPad);
	Up.Y -= 60.0f;
	FTouchPoint Down = Up;
	Down.Y += 120.0f;
	REQUIRE(Pad.Begin(0, Up));
	REQUIRE(Pad.Move(0, Down));
	REQUIRE(Pad.End(0));
	CHECK(Pad.TakeForTick().Buttons == FPadState::Bit(EPadButton::Down));
	REQUIRE(Pad.Begin(0, Center(Layout, ETouchControl::Cross)));
	Pad.Reset();
	CHECK(Pad.TakeForTick().Buttons == 0);
	REQUIRE(Pad.Begin(0, Center(Layout, ETouchControl::Cross)));
	Pad.SetLayout(Layout);
	CHECK(Pad.TakeForTick().Buttons == 0);
}

TEST_CASE("Android Back taps survive a full press and release between game samples", "[Input][Touch]")
{
	FPadLatch Back;
	const FPadState Cancel{FPadState::Bit(EPadButton::Circle)};
	Back.Sample(Cancel);
	Back.Sample({});
	CHECK(Back.TakeForTick().IsHeld(EPadButton::Circle));
	CHECK(Back.TakeForTick().Buttons == 0);
	Back.Sample(Cancel);
	CHECK(Back.TakeForTick().IsHeld(EPadButton::Circle));
	CHECK(Back.TakeForTick().IsHeld(EPadButton::Circle));
	Back.Sample({});
	CHECK(Back.TakeForTick().Buttons == 0);
	const FPadState Hardware{FPadState::Bit(EPadButton::Circle)};
	CHECK(MergePads(Hardware, Back.TakeForTick()).IsHeld(EPadButton::Circle));
	Back.Sample(Cancel);
	Back = {};
	CHECK(Back.TakeForTick().Buttons == 0);
}

TEST_CASE("D-pad ownership follows drags and clears on layout changes", "[Input][Touch]")
{
	const FTouchLayout Layout = FTouchLayout::Build(1600.0f, 720.0f, {});
	FTouchPad Pad;
	Pad.SetLayout(Layout);
	REQUIRE(Pad.Begin(1, Center(Layout, ETouchControl::DPad)));
	CHECK(Pad.Held().Buttons == 0);
	CHECK(Pad.Move(1, {-100.0f, Center(Layout, ETouchControl::DPad).Y}));
	CHECK(Pad.Held().Buttons == FPadState::Bit(EPadButton::Left));
	CHECK_FALSE(Pad.Begin(8, {800.0f, 360.0f}));
	CHECK_FALSE(Pad.Move(8, Center(Layout, ETouchControl::Cross)));
	Pad.SetLayout(FTouchLayout::Build(720.0f, 1600.0f, {}));
	CHECK(Pad.Held().Buttons == 0);
	CHECK_FALSE(Pad.End(1));
}

TEST_CASE("Captured action buttons release when dragged away and retain pointer ownership", "[Input][Touch]")
{
	const FTouchLayout Layout = FTouchLayout::Build(1280.0f, 720.0f, {});
	FTouchPad Pad;
	Pad.SetLayout(Layout);
	REQUIRE(Pad.Begin(5, Center(Layout, ETouchControl::Cross)));
	CHECK(Pad.Move(5, {640.0f, 360.0f}));
	CHECK(Pad.Held().Buttons == 0);
	CHECK(Pad.Move(5, Center(Layout, ETouchControl::Cross)));
	CHECK(Pad.Held().IsHeld(EPadButton::Cross));
	CHECK_FALSE(Pad.Begin(5, {640.0f, 360.0f}));
	CHECK(Pad.Held().Buttons == 0);
}

TEST_CASE("Every PlayStation button is reachable through the authored overlay", "[Input][Touch]")
{
	const FTouchLayout Layout = FTouchLayout::Build(1600.0f, 720.0f, {});
	const std::array<EPadButton, TouchControlCount - 1> Buttons{EPadButton::Triangle, EPadButton::Circle, EPadButton::Cross,
		EPadButton::Square, EPadButton::L1, EPadButton::R1, EPadButton::L2, EPadButton::R2, EPadButton::Select, EPadButton::Start,
		EPadButton::L3, EPadButton::R3};
	FTouchPad Pad;
	Pad.SetLayout(Layout);
	for (std::size_t Index = 1; Index < TouchControlCount; ++Index)
	{
		const auto Control = static_cast<ETouchControl>(Index);
		REQUIRE(Pad.Begin(0, Center(Layout, Control)));
		CHECK(Pad.Held().Buttons == FPadState::Bit(Buttons[Index - 1]));
		Pad.End(0);
	}
}

TEST_CASE("Touch layouts fit the safe area and opacity never changes hit testing", "[Input][Touch]")
{
	for (const FTouchPoint Size : {FTouchPoint{1280, 720}, FTouchPoint{2400, 1080}, FTouchPoint{1920, 1080}})
	{
		FTouchOptions Options;
		Options.IdleOpacity = 0.0f;
		Options.PressedOpacity = 0.0f;
		Options.bReserveSides = true;
		const FTouchLayout Layout = FTouchLayout::Build(Size.X, Size.Y, Options);
		for (const FTouchRect& Rect : Layout.Controls)
		{
			CHECK(Rect.X >= 0);
			CHECK(Rect.Y >= 0);
			CHECK(Rect.X + Rect.Width <= Size.X + 0.01f);
			CHECK(Rect.Y + Rect.Height <= Size.Y + 0.01f);
		}
		CHECK(Layout.Hit(Center(Layout, ETouchControl::Cross)) == ETouchControl::Cross);
		CHECK(Layout.Picture.X > 0.0f);
		CHECK(Layout.Picture.Width < Size.X);
		CHECK(Layout.Picture.Width >= Size.X * 0.5f);
		for (const FTouchRect& Rect : Layout.Controls)
		{
			CHECK((Rect.X + Rect.Width <= Layout.Picture.X || Rect.X >= Layout.Picture.X + Layout.Picture.Width));
		}
	}
	FTouchOptions Moved;
	Moved.Offsets[static_cast<std::size_t>(ETouchControl::Cross)] = {-0.15f, -0.2f};
	const FTouchLayout Original = FTouchLayout::Build(1600, 720, {});
	const FTouchLayout Edited = FTouchLayout::Build(1600, 720, Moved);
	CHECK(Center(Edited, ETouchControl::Cross).X < Center(Original, ETouchControl::Cross).X);
	CHECK(Center(Edited, ETouchControl::Cross).Y < Center(Original, ETouchControl::Cross).Y);
	CHECK_FALSE(FTouchLayout::Build(0, 0, {}).Hit({0, 0}).has_value());
	CHECK_FALSE(FTouchLayout::Build(std::numeric_limits<float>::infinity(), 100, {}).Hit({0, 0}).has_value());
}

TEST_CASE("Touch appearance and edited positions survive settings reload", "[Settings][Touch]")
{
	FPlayerSettings Settings;
	Settings.Touch.IdleOpacity = 0.1f;
	Settings.Touch.PressedOpacity = 0.8f;
	Settings.Touch.Scale = 1.25f;
	Settings.Touch.bReserveSides = true;
	Settings.Touch.Offsets[3] = {-0.1f, 0.05f};
	CHECK(ReadPlayerSettings(WritePlayerSettings(Settings)).Touch == Settings.Touch);
	const FPlayerSettings Clamped = ReadPlayerSettings("touch.idle_opacity = -4\ntouch.pressed_opacity = 2\ntouch.scale = 50\ntouch.cross.offset_x = -5\n");
	CHECK(Clamped.Touch.IdleOpacity == 0.0f);
	CHECK(Clamped.Touch.PressedOpacity == 1.0f);
	CHECK(Clamped.Touch.Scale == 1.4f);
	CHECK(Clamped.Touch.Offsets[3].X == -1.0f);
	CHECK(ReadPlayerSettings("touch.idle_opacity = nan\ntouch.scale = inf\n").Touch == FTouchOptions{});
}

TEST_CASE("Touch decimal settings reject malformed and unrepresentable values", "[Settings][Touch]")
{
	for (const std::string_view Value : {"", "nan", "NaN", "inf", "-inf", "1e99999", "1e-99999", "0.2tail", "0,2", "0x1p-2", "+0.2", "--0.2", "1e", ".", "0.2 0.3"})
	{
		CAPTURE(Value);
		CHECK(ReadPlayerSettings("touch.idle_opacity = " + std::string(Value) + "\n").Touch.IdleOpacity == FTouchOptions{}.IdleOpacity);
	}
	CHECK(ReadPlayerSettings("touch.idle_opacity = 5e-1\n").Touch.IdleOpacity == 0.5f);
	CHECK(ReadPlayerSettings("touch.idle_opacity = .125\n").Touch.IdleOpacity == 0.125f);
	CHECK(ReadPlayerSettings("touch.idle_opacity = -0.5\n").Touch.IdleOpacity == 0.0f);
	CHECK(ReadPlayerSettings("touch.idle_opacity = 1e3\n").Touch.IdleOpacity == 1.0f);
	CHECK(ReadPlayerSettings("touch.idle_opacity = 3.4028235e38\n").Touch.IdleOpacity == 1.0f);
	for (const float Value : {0.0f, std::numeric_limits<float>::denorm_min(), 1e-30f, 0.00000123f, 0.12345678f, 0.3f, std::nextafter(1.0f, 0.0f), 1.0f})
	{
		FPlayerSettings Settings;
		Settings.Touch.IdleOpacity = Value;
		CHECK(ReadPlayerSettings(WritePlayerSettings(Settings)).Touch.IdleOpacity == Value);
	}
}

TEST_CASE("Touch decimal settings remain portable under a comma-decimal locale", "[Settings][Touch]")
{
	class FCommaDecimal final : public std::numpunct<char>
	{
		char do_decimal_point() const override { return ','; }
	};
	struct FRestoreLocale
	{
		std::locale Original;
		~FRestoreLocale() { std::locale::global(Original); }
	} Restore{std::locale()};
	std::locale::global(std::locale(Restore.Original, new FCommaDecimal));
	CHECK(ReadPlayerSettings("touch.idle_opacity = 0.125\n").Touch.IdleOpacity == 0.125f);
	CHECK(ReadPlayerSettings("touch.idle_opacity = 0,125\n").Touch.IdleOpacity == FTouchOptions{}.IdleOpacity);
	FPlayerSettings Settings;
	Settings.Touch.IdleOpacity = 0.125f;
	CHECK(ReadPlayerSettings(WritePlayerSettings(Settings)).Touch == Settings.Touch);
}
