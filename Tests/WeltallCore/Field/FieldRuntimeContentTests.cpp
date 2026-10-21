#include "Field/FieldPartySprites.h"
#include "Field/FieldRuntime.h"
#include "Game/GameState.h"
#include "Game/GameStateNames.h"
#include "Game/PsxRandom.h"
#include "Input/KernelPad.h"
#include "Support/FieldOracleMap.h"
#include "Support/FieldSongsModel.h"
#include "Support/GameDiscs.h"
#include "Support/GameText.h"
#include "Timing/PlayClock.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <vector>

using namespace Weltall;

// The field's own screens between its ticks on a real map, as its main loop runs them: Select's
// picture of the map and the pause, whose frames stand the field still, and the menu it stops at.
namespace
{
	/** Physical buttons in the controller's report word. */
	constexpr uint16 SelectReport = 0x0100;
	constexpr uint16 StartReport = 0x0800;
	constexpr uint16 SquareReport = 0x0080;

	/** The sound's pause as the field asks for it: the pauses and the resumes counted. */
	struct FCountingSoundPause final : IFieldSoundPause
	{
		int32 Pauses = 0;
		int32 Resumes = 0;

		void PauseSound() override { ++Pauses; }
		void ResumeSound() override { ++Resumes; }
	};

	struct FRuntimeSession
	{
		FGameState Game;
		FPsxRandom Random{0x1D5C6A31};
		Testing::FFieldSongsModel Songs;
		FPartySpriteFiles Party;
		FKernelPad Pad;
		FPlayClock Clock;
		FGameStateNames Names{Game};
		FCountingSoundPause SoundPause;
		FFieldRuntime Runtime;

		FRuntimeSession(const Testing::FOracleFieldFiles& Files, const FMesResident& Resident)
			: Game(Files.NewGame), Party(Files.Disc->Image(), Files.Index), Runtime(Services(Files, Resident))
		{
		}

		FFieldServices Services(const Testing::FOracleFieldFiles& Files, const FMesResident& Resident)
		{
			FFieldServices Result;
			Result.Image = &Files.Disc->Image();
			Result.Index = &Files.Index;
			Result.Arctangents = &Files.Arctangents;
			Result.Game = &Game;
			Result.Random = &Random;
			Result.Songs = &Songs;
			Result.SoundPause = &SoundPause;
			Result.PartySprites = &Party;
			Result.Pad = &Pad;
			Result.Clock = &Clock;
			Result.Text.Font = &Resident.Font;
			Result.Text.Tables = &Resident.NameTables;
			Result.Text.State = &Names;
			Result.Text.ButtonMap = MesKernelButtonMap;
			Result.Options = FEventOptions::Original();
			return Result;
		}

		/** A frame of the field with Report pressed at its first vertical blank only. */
		void Frame(uint16 Report = 0)
		{
			std::vector<FPadState> Vblanks(static_cast<std::size_t>(Runtime.VblanksPerTick()));
			Vblanks[0] = {static_cast<uint16>(Report >> 8 | (Report & 0xFF) << 8)};
			const auto Error = Runtime.Tick(Vblanks);
			REQUIRE_FALSE(Error);
		}

		/** Frames without a button until the main loop runs with the player in control of the leader. */
		void RunToPlayerControl()
		{
			for (int32 Frame = 0; Frame < 400; ++Frame)
			{
				this->Frame();
				if (Frame >= FFieldRuntime::EntryTicks && Runtime.Context().bPlayerControlled)
				{
					return;
				}
			}
			FAIL("the player never takes control");
		}
	};
}

TEST_CASE("Select's picture of the map brightens, waits for Select and darkens while the field stands still", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	Session.Game.InventoryItems(0)[10] = 0x61;
	Session.Game.InventoryCounts(0)[10] = 1;
	FFieldRuntime& Runtime = Session.Runtime;
	REQUIRE_FALSE(Runtime.EnterMap(239));
	Session.RunToPlayerControl();

	const FEventContext& Context = Runtime.Context();
	const FFieldMapPicture& Picture = Context.Render.MapPicture;
	Session.Frame(SelectReport);
	REQUIRE(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK_FALSE(Picture.bShown);
	CHECK(Picture.File == 0x7FE);
	const std::array<int32, 3> Leader = Context.States[Context.LeaderActor].Position;
	const uint8 Buffer = Context.DrawBuffer;
	const uint32 PlayTime = Session.Game.PlayTime();

	for (int32 Frame = 0; Frame < 16; ++Frame)
	{
		CHECK(Runtime.VblanksPerTick() == 2);
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::MapPicture);
		CHECK(Picture.bShown);
		CHECK(Picture.Brightness == 8 * Frame);
	}
	// Held at full brightness until Select comes again; each frame swaps the draw buffers.
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::MapPicture);
		CHECK(Picture.Brightness == 0x80);
	}
	Session.Frame(SelectReport);
	CHECK(Picture.Brightness == 0x80);
	for (int32 Frame = 0; Frame < 16; ++Frame)
	{
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::MapPicture);
		CHECK(Picture.Brightness == 0x80 - 8 * Frame);
	}
	CHECK(Context.DrawBuffer == (Buffer ^ 1));
	CHECK(Session.Game.PlayTime() == PlayTime + 2 * (16 + 5 + 16));
	CHECK(Context.States[Context.LeaderActor].Position == Leader);

	Session.Frame();
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK_FALSE(Picture.bShown);
}

TEST_CASE("Select shows no picture where the party lacks the map's item", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	REQUIRE_FALSE(Session.Runtime.EnterMap(239));
	Session.RunToPlayerControl();
	Session.Frame(SelectReport);
	Session.Frame();
	CHECK(Session.Runtime.LastFrame() == EFieldFrame::Tick);
}

TEST_CASE("Start pauses the field until Start comes again, and the pause's frames count no play time", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	FFieldRuntime& Runtime = Session.Runtime;
	REQUIRE_FALSE(Runtime.EnterMap(239));
	Session.RunToPlayerControl();

	Session.Frame(StartReport);
	REQUIRE(Runtime.LastFrame() == EFieldFrame::Tick);
	Session.Frame();
	CHECK(Runtime.LastFrame() == EFieldFrame::Paused);
	const uint32 PlayTime = Session.Game.PlayTime();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		CHECK(Runtime.VblanksPerTick() == 2);
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::Paused);
	}
	// Start again: the pause ends and the tick runs in the same frame, the pause's vertical blanks uncounted.
	CHECK(Session.SoundPause.Pauses == 1);
	CHECK(Session.SoundPause.Resumes == 0);
	Session.Frame(StartReport);
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK(Session.Game.PlayTime() == PlayTime);
	CHECK(Session.SoundPause.Resumes == 1);
}

TEST_CASE("After a lost battle the field copies the draw buffer showing the picture of the screen to where the title's panorama reads it", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	FFieldRuntime& Runtime = Session.Runtime;
	FFieldEntry Defeat;
	Defeat.bAfterDefeat = true;
	REQUIRE_FALSE(Runtime.EnterMap(490, Runtime.KernelState(), Defeat));
	const FFieldRenderState& Render = Runtime.Context().Render;
	REQUIRE(Render.PictureMoves.size() == 1);
	const FFieldVramOperation& Copy = Render.PictureMoves[0];
	CHECK(Copy.Kind == EFieldVramChange::Copy);
	CHECK((Copy.Rect.X == 0x2C0 && Copy.Rect.Y == 0x100 && Copy.Rect.Width == 0x140 && Copy.Rect.Height == 0xE0));
	CHECK((Copy.ToX == 0x200 && Copy.ToY == 0));
	CHECK(Copy.Brightness == 0x80);
	// The map's first tick has none.
	Session.Frame();
	CHECK(Render.PictureMoves.empty());

	// The usual entry copies nothing.
	FRuntimeSession Usual(*Files, Resident);
	REQUIRE_FALSE(Usual.Runtime.EnterMap(490));
	CHECK(Usual.Runtime.Context().Render.PictureMoves.empty());
}

TEST_CASE("Without a controller the field waits, its sound paused, until one is back, and the wait counts no play time", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	FFieldRuntime& Runtime = Session.Runtime;
	REQUIRE_FALSE(Runtime.EnterMap(239));
	Session.RunToPlayerControl();

	// The pass that finds no controller runs no tick: the wait's frames last two vertical blanks and show the pause.
	const uint32 PlayTime = Session.Game.PlayTime();
	const uint32 Vblanks = static_cast<uint32>(Runtime.VblanksPerTick());
	Session.Pad.SetConnected(false);
	Session.Frame();
	CHECK(Runtime.LastFrame() == EFieldFrame::Paused);
	CHECK(Session.SoundPause.Pauses == 1);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		CHECK(Runtime.VblanksPerTick() == 2);
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::Paused);
	}
	// A controller back: the sound goes on and the tick runs in the same frame; only the first frame's
	// vertical blanks, before the pass found none, count.
	Session.Pad.SetConnected(true);
	Session.Frame();
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK(Session.SoundPause.Resumes == 1);
	CHECK(Session.Game.PlayTime() == PlayTime + Vblanks);
}

TEST_CASE("Start pressed as a controller comes back pauses the field at once", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	FFieldRuntime& Runtime = Session.Runtime;
	REQUIRE_FALSE(Runtime.EnterMap(239));
	Session.RunToPlayerControl();

	Session.Pad.SetConnected(false);
	Session.Frame();
	REQUIRE(Runtime.LastFrame() == EFieldFrame::Paused);
	// The wait reads the pad each frame: the pause's check after it finds Start in the words it read last.
	Session.Pad.SetConnected(true);
	Session.Frame(StartReport);
	CHECK(Runtime.LastFrame() == EFieldFrame::Paused);
	CHECK(Session.SoundPause.Pauses == 2);
	CHECK(Session.SoundPause.Resumes == 1);
	Session.Frame(StartReport);
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK(Session.SoundPause.Resumes == 2);
}

TEST_CASE("The player's menu stops the field on a tick drawing into buffer 0, and the field goes on after it", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	FFieldRuntime& Runtime = Session.Runtime;
	REQUIRE_FALSE(Runtime.EnterMap(239));
	Session.RunToPlayerControl();

	// Physical Square is the menu's button on the US discs.
	Session.Frame(SquareReport);
	for (int32 Frame = 0; Frame < 2 && Runtime.VblanksPerTick() != 1; ++Frame)
	{
		Session.Frame();
	}
	// The tick that opened it drew into buffer 0; its shading's frames last a vertical blank each.
	REQUIRE(Runtime.VblanksPerTick() == 1);
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK(Runtime.Context().DrawBuffer == 0);
	// The field's picture darkens a step a vertical blank, 32 steps, before the menu is asked for.
	const FFieldMenuShade& Shade = Runtime.Context().Render.MenuShade;
	const uint32 PlayTime = Session.Game.PlayTime();
	for (int32 Step = 0; Step < 32; ++Step)
	{
		REQUIRE(Runtime.Request().Kind == EFieldRequest::None);
		CHECK(Runtime.VblanksPerTick() == 1);
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::MenuShade);
		CHECK(Shade.bShown);
		CHECK(Shade.Level == Step);
	}
	CHECK(Session.Game.PlayTime() == PlayTime + 32);
	const FFieldRequest& Request = Runtime.Request();
	REQUIRE(Request.Kind == EFieldRequest::Menu);
	CHECK(Request.MenuMode == 0);
	CHECK(Request.bMenuByButton);
	CHECK(Runtime.Context().DrawBuffer == 0);
	std::vector<FPadState> Vblanks(static_cast<std::size_t>(Runtime.VblanksPerTick()));
	CHECK(Runtime.Tick(Vblanks));

	// After it, two steps hold the darkest, 33 lighten the picture back, then the field's main loop goes on.
	REQUIRE_FALSE(Runtime.ResumeAfterMenu(0));
	CHECK(Runtime.Request().Kind == EFieldRequest::None);
	for (int32 Step = 0; Step < 35; ++Step)
	{
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::MenuShade);
		CHECK(Shade.Level == std::max(31 - std::max(Step - 2, 0), 0));
	}
	CHECK(Runtime.Context().MenuRequest == FEventContext::NoMenu);
	CHECK(Runtime.Context().MenusPending == 0);
	CHECK_FALSE(Runtime.Context().bFrameVariablesFrozen);
	CHECK(Runtime.Context().DrawBuffer == 0);
	CHECK_FALSE(Runtime.Context().Render.Panorama.bHidden);
	CHECK(Runtime.ResumeAfterRequest());
	Session.Frame();
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
	CHECK_FALSE(Shade.bShown);
}

TEST_CASE("After a menu the scripts keep dark, the field's picture goes on toward black and its backdrop stays hidden", "[Field][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	std::optional<Testing::FOracleFieldFiles> Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files));
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	FRuntimeSession Session(*Files, Resident);
	FFieldRuntime& Runtime = Session.Runtime;
	REQUIRE_FALSE(Runtime.EnterMap(239));
	Session.RunToPlayerControl();
	Session.Frame(SquareReport);
	for (int32 Frame = 0; Frame < 40 && Runtime.Request().Kind == EFieldRequest::None; ++Frame)
	{
		Session.Frame();
	}
	REQUIRE(Runtime.Request().Kind == EFieldRequest::Menu);
	// FE 86: the scripts keep the screen dark after the menu.
	Runtime.Context().ReturnFade = 1;
	REQUIRE_FALSE(Runtime.ResumeAfterMenu(0));
	const FFieldMenuShade& Shade = Runtime.Context().Render.MenuShade;
	for (int32 Step = 0; Step < 33; ++Step)
	{
		Session.Frame();
		CHECK(Runtime.LastFrame() == EFieldFrame::MenuShade);
		CHECK(Shade.Level == (Step < 2 ? 31 : 30 + Step));
	}
	CHECK(Runtime.Context().Render.Panorama.bHidden);
	CHECK(Runtime.ResumeAfterRequest());
	Session.Frame();
	CHECK(Runtime.LastFrame() == EFieldFrame::Tick);
}
