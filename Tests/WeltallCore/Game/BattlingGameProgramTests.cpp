#include "Battling/BattlingFight.h"
#include "Battling/BattlingMenus.h"
#include "Game/BattlingGameFiles.h"
#include "Game/BattlingGameProgram.h"
#include "Game/FieldProgram.h"
#include "Game/GameState.h"
#include "Game/KernelState.h"
#include "Sound/GameSoundSystem.h"
#include "Sound/SoundDriver.h"
#include "Support/GameDiscs.h"
#include "Support/GameLoopSession.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cmath>
#include <string>

using namespace Weltall;

// The Battling program as the game's loop runs it: started from the six bytes a field script hands the kernel, its bout run
// to its end with the session's pad, and the field back with the result its scripts read.

namespace
{
	constexpr FPadState NoButtons{};
	/** A forest map, whose scripts ask for nothing as the test asks for the arena. */
	constexpr uint16 ForestMap = 15;
	constexpr uint16 ArenaEffectsId = 6;
	constexpr uint8 FeiGear = 0;
	constexpr uint8 Ganador = 17;
	constexpr uint8 TinRobo = 28;
	constexpr uint8 ElFenrir = 13;
	constexpr uint8 StateTutorial = 7;
	constexpr uint8 ResultScriptedEnding = 0x7F;
	constexpr uint32 SettingsDefaults = 1 | 2 << 6;
	constexpr uint32 SettingsArgento = 0x10000;

	std::string Stopped(const Testing::FGameLoopSession& Game)
	{
		return Game.Loop->StopReason() ? Game.Loop->StopReason()->Message : std::string();
	}

	/** Enters the forest and asks for the arena as a script's FE BF does; the loop run until the kernel starts the program. */
	void StartArena(Testing::FGameLoopSession& Game, const std::array<uint8, 6>& Parameters, bool bStops = false)
	{
		FGameState& State = Game.Session->Game();
		State.SetParty({0, 1, 2});
		State.SetJoinedCharacters(0x1FF);
		Game.EnterField(ForestMap);
		FFieldProgram* Field = Game.Loop->Field();
		REQUIRE(Field != nullptr);
		Game.RunUntil([&] { return Field->Runtime().Context().bFieldRunning; }, 100);
		Game.Frame();
		FEventContext& Context = Game.Loop->Field()->Runtime().Context();
		Context.Requests.BattlingParameters = Parameters;
		Context.Requests.bLeavingField = true;
		Context.Requests.bBattling = true;
		Game.RunUntil([&] { return Game.Loop->Program() == EGameProgram::Battling; }, 8, NoButtons, bStops);
	}

	/** The pad of a player who walks at the opponent and strikes: a button or two held for a few vertical blanks, then let go. */
	FPadState Striking(int32 Frame)
	{
		constexpr std::array<EPadButton, 4> Buttons{EPadButton::Square, EPadButton::Triangle, EPadButton::Cross, EPadButton::Square};
		FPadState Held{FPadState::Bit(EPadButton::Right)};
		if (Frame % 6 < 3)
		{
			Held.Buttons = static_cast<uint16>(Held.Buttons | FPadState::Bit(Buttons[static_cast<std::size_t>(Frame / 6 % 4)]));
		}
		return Held;
	}

	bool IsDefeated(const FGameState& State, uint8 Gear)
	{
		return (State.ArenaDefeatedGears()[Gear >> 3] & 1u << (Gear & 7)) != 0;
	}
}

TEST_CASE("The story's arena bout runs from the field's six bytes to its result and the field again", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	// Weltall against Tin Robo's computer at the easy level, a set of one win.
	StartArena(*Game, {0, 0, FeiGear, TinRobo, 1, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	CHECK(Game->Session->Kernel().BattlingParameters[0] == 0);
	CHECK(Game->Session->Kernel().FieldTrips == 1);
	CHECK(Game->Session->Sound().Sound()->Lock()->FindEffects(ArenaEffectsId) == &Game->Session->BattlingFiles().Effects);
	// A bout started by its bytes reads no saved settings.
	CHECK(Game->Session->Game().ArenaSettings() == 0);

	// The versus screen, ended by a button on its first pass: then the load's four steps a vertical blank each, the fire
	// stepped at each.
	CHECK(Arena->Stage() == EBattlingGameStage::Versus);
	CHECK(Arena->Gears() == std::array<uint8, 2>{FeiGear, TinRobo});
	CHECK(Arena->Fight().Fire.IsBurning());
	constexpr std::array<int32, 5> VersusBlanks{2, 2, 1, 1, 1};
	for (std::size_t Pass = 0; Pass < VersusBlanks.size(); ++Pass)
	{
		CHECK(Arena->Stage() == EBattlingGameStage::Versus);
		CHECK(Game->Loop->VblanksPerFrame() == VersusBlanks[Pass]);
		REQUIRE_FALSE(Game->Frame(Pass == 0 ? FPadState{FPadState::Bit(EPadButton::Cross)} : NoButtons));
	}
	CHECK(Arena->Stage() == EBattlingGameStage::Bout);
	CHECK(Game->Loop->VblanksPerFrame() == 1);

	// The bout fades in through the fire over 64 passes, the gears at twice their speed meanwhile.
	REQUIRE_FALSE(Game->Frame());
	const FBattlingFight& Fight = Arena->Fight();
	CHECK(Fight.Pass == 7);
	CHECK(Fight.Round == 1);
	CHECK(Fight.FireFade == 0xFF - 4);
	CHECK(Fight.bFireActive);
	CHECK(Fight.Settings.MotionSpeed == 0x200);
	CHECK(Fight.Settings.Mode == EBattlingMode::VersusComputer);
	CHECK(Fight.Settings.WinsPerSet == 1);
	CHECK(Game->Loop->VblanksPerFrame() == 2);
	// The pass's frame: the sky, the arena and both gears, textured from the program's VRAM; the next follows it for the display to draw between.
	const FBattlingFrame* Drawn = Arena->LatestFrame();
	REQUIRE(Drawn != nullptr);
	CHECK(Drawn->Pass == 7);
	CHECK(Arena->PreviousFrame() == nullptr);
	int32 GearPolygons = 0;
	int32 SkyPolygons = 0;
	for (const FWorldMapPolygon& Polygon : Drawn->Scene.Polygons)
	{
		GearPolygons += Polygon.Layer == EWorldMapLayer::Gear ? 1 : 0;
		SkyPolygons += Polygon.Layer == EWorldMapLayer::Sky ? 1 : 0;
	}
	CHECK(GearPolygons > 100);
	CHECK(SkyPolygons == 1);
	CHECK(Drawn->Scene.Polygons.size() > static_cast<std::size_t>(GearPolygons) + 100);
	// Weltall's page across VRAM's top holds its textures.
	bool bGearTexels = false;
	for (int32 X = 0x3C0; X < 0x400 && !bGearTexels; ++X)
	{
		bGearTexels = Arena->Vram().At(X, 8) != 0;
	}
	CHECK(bGearTexels);
	// The HUD over the scene, its pictures in VRAM: the names' palette, each icon's palette row and the names.
	CHECK(Drawn->Screen.size() > 30);
	CHECK(Arena->Vram().At(BattlingNamePalettePlace.X, BattlingNamePalettePlace.Y) == 0x8000);
	bool bIconColours = false;
	bool bNames = false;
	for (int32 X = 0; X < BattlingNamesPlace.Width; ++X)
	{
		bIconColours = bIconColours || Arena->Vram().At(X, BattlingIconPaletteRow + 1) != 0;
		for (int32 Row = 0; Row < BattlingNamesPlace.Height; ++Row)
		{
			bNames = bNames || Arena->Vram().At(BattlingNamesPlace.X + X, BattlingNamesPlace.Y + Row) != 0;
		}
	}
	CHECK(bIconColours);
	CHECK(bNames);
	CHECK(Game->Session->Kernel().LightMatrix.M != FGteMatrix{}.M);
	for (int32 Pass = 1; Pass < 64; ++Pass)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	REQUIRE(Arena->PreviousFrame() != nullptr);
	CHECK(Arena->PreviousFrame()->Pass + 1 == Arena->LatestFrame()->Pass);
	CHECK(Fight.FireFade == 0);
	CHECK_FALSE(Fight.Fire.IsBurning());
	CHECK(Fight.Settings.MotionSpeed == 0x100);

	// Fought until the set is over and the program leaves: the field next, with the result its scripts read. A won set shows
	// the winner screen first, its frames twice as many columns across.
	const int32 Runs = Game->Loop->ProgramRuns();
	bool bWinnerShown = false;
	for (int32 Frame = 0; Frame < 20000 && Game->Loop->ProgramRuns() == Runs; ++Frame)
	{
		INFO("frame " << Frame);
		REQUIRE_FALSE(Game->Frame(Striking(Frame)));
		const FBattlingGameProgram* Running = Game->Loop->Battling();
		if (Running != nullptr && Running->LatestFrame() != nullptr)
		{
			bWinnerShown = bWinnerShown || Running->LatestFrame()->Columns == 2 * FBattlingFrame::ScreenWidth;
		}
	}
	INFO(Stopped(*Game));
	REQUIRE(Game->Loop->ProgramRuns() != Runs);
	REQUIRE(Game->Loop->Program() == EGameProgram::Field);
	const FKernelState& Kernel = Game->Session->Kernel();
	const uint8 Result = Kernel.Field.BattlingResult;
	INFO("result " << static_cast<int32>(Result));
	const bool bWon = Result >= 1 && Result <= 3;
	const bool bLost = Result >= 0x81 && Result <= 0x88;
	CHECK((bWon || bLost));
	CHECK(bWinnerShown == bWon);
	// A round won against the computer marks its gear in the game state, which the saves keep.
	CHECK(IsDefeated(Game->Session->Game(), TinRobo) == bWon);
	CHECK(Kernel.BattlingParameters[0] == 1);
	CHECK(Kernel.FieldTrips == 0);
	CHECK(Game->Session->Sound().Sound()->Lock()->FindEffects(ArenaEffectsId) == nullptr);
	const FFieldProgram* Field = Game->Loop->Field();
	REQUIRE(Field != nullptr);
	CHECK(Field->Runtime().Context().RunningMap == ForestMap);
	CHECK(Field->Runtime().Context().BattlingResult == Result);
}

TEST_CASE("The arena's shadows lie on floors whose up its camera's second column names, the eye above them", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 0, FeiGear, TinRobo, 1, 0});
	INFO(Stopped(*Game));
	// The display's floor rule takes the world's up in view space as the camera's second column turned the other way.
	int32 Shadows = 0;
	for (int32 Frame = 0; Frame < 1200 && Game->Loop->Battling() != nullptr; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame(Striking(Frame)));
		const FBattlingGameProgram* Arena = Game->Loop->Battling();
		const FBattlingFrame* Drawn = Arena != nullptr ? Arena->LatestFrame() : nullptr;
		if (Drawn == nullptr || Drawn->Columns != FBattlingFrame::ScreenWidth)
		{
			continue;
		}
		const FGteMatrix& Camera = Drawn->Scene.Camera;
		std::array<double, 3> Up{-static_cast<double>(Camera.M[0][1]), -static_cast<double>(Camera.M[1][1]), -static_cast<double>(Camera.M[2][1])};
		const double UpLength = std::sqrt(Up[0] * Up[0] + Up[1] * Up[1] + Up[2] * Up[2]);
		for (const FWorldMapPolygon& Polygon : Drawn->Scene.Polygons)
		{
			if (Polygon.Layer != EWorldMapLayer::Shadow)
			{
				continue;
			}
			const auto Edge = [&Polygon](std::size_t To) {
				return std::array<double, 3>{Polygon.View[To][0] - Polygon.View[0][0], Polygon.View[To][1] - Polygon.View[0][1], Polygon.View[To][2] - Polygon.View[0][2]};
			};
			const std::array<double, 3> A = Edge(1);
			const std::array<double, 3> B = Edge(2);
			const std::array<double, 3> Normal{A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
			const double NormalLength = std::sqrt(Normal[0] * Normal[0] + Normal[1] * Normal[1] + Normal[2] * Normal[2]);
			const double Facing = (Normal[0] * Up[0] + Normal[1] * Up[1] + Normal[2] * Up[2]) / (NormalLength * UpLength);
			const double Above = -(Up[0] * Polygon.View[0][0] + Up[1] * Polygon.View[0][1] + Up[2] * Polygon.View[0][2]) / UpLength;
			INFO("frame " << Frame << ": the shadow's floor at " << Facing << " to the camera's up, the eye " << Above << " above it");
			// The shadow lies on the ground's slope under the gear, up to some 40 degrees in the arena's hills; either winding of its corners.
			CHECK(std::abs(Facing) > 0.7);
			CHECK(Above > 0.0);
			++Shadows;
		}
	}
	CHECK(Shadows > 100);
}

TEST_CASE("The arena's versus screen stays two seconds unless a button ends it", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 0, FeiGear, TinRobo, 1, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	// Sixty passes of two vertical blanks with no button, then the load's four of one.
	int32 Blanks = 0;
	int32 Passes = 0;
	while (Arena->Stage() == EBattlingGameStage::Versus && Passes < 200)
	{
		Blanks += Game->Loop->VblanksPerFrame();
		REQUIRE_FALSE(Game->Frame());
		++Passes;
	}
	CHECK(Passes == 64);
	CHECK(Blanks == 2 + 60 * 2 + 3);
	REQUIRE_FALSE(Game->Frame());
	CHECK(Arena->Fight().Pass == 1 + 64 + 1);
	CHECK(Arena->Fight().FireFade == 0xFF - 4);
}

TEST_CASE("The arena's pause shows its menu over the screen it froze and blurs it once more for each L2", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 0, FeiGear, TinRobo, 1, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	// Past the fade in and the round's intro, into the fight.
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	const FBattlingFight& Fight = Arena->Fight();
	REQUIRE(Fight.State == 1);
	const uint32 BeforePause = Arena->LatestFrame()->Pass;

	// Start opens the pause in the fight's tick, which still draws its frame; the picture is the frame shown before it.
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Start)}));
	CHECK(Fight.State == 0);
	REQUIRE(Arena->PausedPicture() != nullptr);
	CHECK(Arena->PausedPicture()->Pass == BeforePause);
	CHECK(Arena->LatestFrame()->PictureBlurs == 0);
	CHECK(Game->Loop->VblanksPerFrame() == 2);

	// The paused passes, one vertical blank each: the menu over the picture blurred once, the cursor on CONTINUE BOUT.
	REQUIRE_FALSE(Game->Frame());
	const FBattlingFrame* Paused = Arena->LatestFrame();
	CHECK(Paused->PictureBlurs == 1);
	CHECK(Paused->Scene.Polygons.empty());
	CHECK(Paused->Screen.size() > 20);
	CHECK(Game->Loop->VblanksPerFrame() == 1);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::L2)}));
	REQUIRE_FALSE(Game->Frame());
	CHECK(Arena->LatestFrame()->PictureBlurs == 2);
	CHECK(Fight.Menus.HelpShown[0] == 13);
	CHECK(Arena->LatestFrame()->TextButtons.empty());

	// GIVE UP asks "OK? ((Circle) Button cancels)": the frame tells where the help line shows the button, for the
	// display to draw the player's own there. That button goes back to the pause's menu.
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Down)}));
	REQUIRE_FALSE(Game->Frame());
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	REQUIRE_FALSE(Game->Frame());
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.Menus.Current == EBattlingMenu::GiveUp);
	CHECK(Fight.Menus.HelpShown[0] == 42);
	REQUIRE(Arena->LatestFrame()->TextButtons.size() == 1);
	const FTextButtonCell Asked = Arena->LatestFrame()->TextButtons[0];
	CHECK(Asked.Button == EPadButton::Circle);
	CHECK(Asked.Width == 12);
	REQUIRE(Asked.Primitive < Arena->LatestFrame()->Screen.size());
	CHECK(Arena->LatestFrame()->Screen[Asked.Primitive].IsRectangle());
	CHECK(Asked.OffsetX + Asked.Width < Arena->LatestFrame()->Screen[Asked.Primitive].Width);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Circle)}));
	REQUIRE_FALSE(Game->Frame());
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.Menus.Current == EBattlingMenu::Pause);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Up)}));
	REQUIRE_FALSE(Game->Frame());
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.Menus.HelpShown[0] == 13);
	CHECK(Arena->LatestFrame()->TextButtons.empty());

	// Continuing draws the picture once more, with no menu, then the fight goes on.
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	CHECK(Fight.State == 1);
	CHECK(Arena->LatestFrame()->PictureBlurs == 2);
	CHECK(Arena->LatestFrame()->Screen.empty());
	REQUIRE_FALSE(Game->Frame());
	CHECK(Arena->LatestFrame()->PictureBlurs == 0);
	CHECK_FALSE(Arena->LatestFrame()->Scene.Polygons.empty());
}

TEST_CASE("The arena's tutorial starts without the versus screen", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 2, FeiGear, TinRobo, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	CHECK(Arena->Stage() == EBattlingGameStage::Bout);
	CHECK_FALSE(Arena->Fight().Fire.IsBurning());
	CHECK(Game->Loop->VblanksPerFrame() == 2);
	for (int32 Frame = 0; Frame < 400; ++Frame)
	{
		INFO("frame " << Frame);
		REQUIRE_FALSE(Game->Frame());
	}
	const FBattlingFight& Fight = Arena->Fight();
	CHECK(Fight.Pass == 401);
	CHECK(Fight.State == StateTutorial);
	CHECK(Fight.Settings.Mode == EBattlingMode::Tutorial);
	CHECK(Fight.FireFade == 0);
	CHECK(Fight.Settings.MotionSpeed == 0x100);
	// The welcome is typed in the kernel's window.
	CHECK(Fight.Tutorial.Message >= 0);
	CHECK(Game->Loop->Stage() == EGameLoopStage::Program);
}

TEST_CASE("The arena's tutorial tells which buttons its messages show", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 2, FeiGear, TinRobo, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	// The welcome, then "Choose a topic to learn about and press the (Cross) Button.": the frame names the glyph's cell in
	// the line's sprite, for the display to show the player's button there.
	int32 Shown = 0;
	for (int32 Frame = 0; Frame < 1500 && Shown < 10; ++Frame)
	{
		INFO("frame " << Frame);
		REQUIRE_FALSE(Game->Frame(Frame % 40 == 0 ? FPadState{FPadState::Bit(EPadButton::Cross)} : NoButtons));
		const FBattlingFrame* Latest = Arena->LatestFrame();
		REQUIRE(Latest != nullptr);
		for (const FTextButtonCell& Cell : Latest->TextButtons)
		{
			REQUIRE(Cell.Primitive < Latest->Screen.size());
			const FMenuPrimitive& Line = Latest->Screen[Cell.Primitive];
			REQUIRE(Line.IsRectangle());
			REQUIRE(Cell.OffsetX + Cell.Width <= Line.Width);
			Shown += Cell.Button == EPadButton::Cross && Cell.Width == 12 ? 1 : 0;
		}
	}
	CHECK(Shown == 10);
}

TEST_CASE("The story's scripted bout ends in the breakdown and hands the field its result", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {2, 0, FeiGear, Ganador, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	// The scripted bout reads the saved settings, and writes the defaults where none were.
	CHECK(Game->Session->Game().ArenaSettings() == SettingsDefaults);
	CHECK(Arena->Stage() == EBattlingGameStage::Versus);
	CHECK(Arena->Fight().Settings.bScripted);

	// A player who strikes: either gear's HP falls to the breakdown's mark, the ending plays and the program leaves.
	const int32 Runs = Game->Loop->ProgramRuns();
	for (int32 Frame = 0; Frame < 20000 && Game->Loop->ProgramRuns() == Runs; ++Frame)
	{
		INFO("frame " << Frame);
		REQUIRE_FALSE(Game->Frame(Striking(Frame)));
	}
	INFO(Stopped(*Game));
	REQUIRE(Game->Loop->Program() == EGameProgram::Field);
	CHECK(Game->Session->Kernel().Field.BattlingResult == ResultScriptedEnding);
	CHECK_FALSE(IsDefeated(Game->Session->Game(), Ganador));
	CHECK(Game->Session->Sound().Sound()->Lock()->FindEffects(ArenaEffectsId) == nullptr);
}

TEST_CASE("The arena's menus start a bout, take its set back to them and leave by EXIT", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {1, 0, 0, 0, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	const FBattlingFight& Fight = Arena->Fight();
	// The menus read the saved settings, writing the defaults where none were; the top menu over the fire, a vertical
	// blank a pass.
	CHECK(Game->Session->Game().ArenaSettings() == SettingsDefaults);
	CHECK(Arena->Stage() == EBattlingGameStage::Menus);
	CHECK(Fight.Menus.Current == EBattlingMenu::Top);
	CHECK(Fight.Settings.WinsPerSet == 2);
	REQUIRE(Fight.Menus.Roster.size() >= 2);
	CHECK(Game->Loop->VblanksPerFrame() == 1);
	REQUIRE_FALSE(Game->Frame());
	REQUIRE(Arena->LatestFrame() != nullptr);
	CHECK(Arena->LatestFrame()->Scene.Polygons.empty());
	CHECK(Arena->LatestFrame()->Screen.size() > 20);
	// The help line shows the item the cursor stood on a pass before.
	CHECK(Fight.Menus.HelpShown[0] == 0);
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.Menus.HelpShown[0] == 1);

	const auto Press = [&](EPadButton Button) {
		REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(Button)}));
		REQUIRE_FALSE(Game->Frame());
	};
	// BONUS BATTLING, then PLAYER1 VS COM: the gear choice, both carousels drawn.
	Press(EPadButton::Cross);
	CHECK(Fight.Menus.Current == EBattlingMenu::Bonus);
	Press(EPadButton::Cross);
	CHECK(Fight.Menus.Current == EBattlingMenu::PlayerOneGear);
	CHECK(Fight.Settings.Mode == EBattlingMode::VersusComputer);
	CHECK(Fight.Menus.Gears.PortraitCount == 8);
	CHECK(Arena->LatestFrame()->Screen.size() > 8);
	// Player 1's gear, then the computer's (player 1 chooses it): the next pass takes the bout, the versus screen after.
	Press(EPadButton::Cross);
	CHECK(Fight.Menus.Gears.Chosen == 1);
	Press(EPadButton::Cross);
	CHECK(Fight.Menus.bBoutChosen);
	CHECK(Arena->Stage() == EBattlingGameStage::Versus);
	CHECK(Arena->Gears() == std::array<uint8, 2>{Fight.Menus.Roster[0], Fight.Menus.Roster[1]});
	CHECK(Fight.Settings.Computer == std::array<bool, 2>{false, true});

	// A button ends the read's wait, then the load's four passes; the set begins through the fire.
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		CHECK(Arena->Stage() == EBattlingGameStage::Versus);
		REQUIRE_FALSE(Game->Frame());
	}
	CHECK(Arena->Stage() == EBattlingGameStage::Bout);
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.State == 1);
	CHECK(Fight.Round == 1);
	CHECK(Fight.bFireActive);
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	CHECK_FALSE(Arena->LatestFrame()->Scene.Polygons.empty());

	// The pause, GIVE UP and YES: the set ends, the next pass back in the menus, the fire lit again.
	Press(EPadButton::Start);
	CHECK(Fight.State == 0);
	Press(EPadButton::Down);
	Press(EPadButton::Cross);
	CHECK(Fight.Menus.Current == EBattlingMenu::GiveUp);
	Press(EPadButton::Up);
	Press(EPadButton::Cross);
	CHECK(Arena->Stage() == EBattlingGameStage::Menus);
	CHECK(Fight.Menus.Current == EBattlingMenu::Top);
	CHECK(Fight.FireFade == 0xFF);
	CHECK(Arena->LatestFrame()->Scene.Polygons.empty());

	// The level changed in bonus battling's menu, then EXIT: the settings saved, the field next.
	Press(EPadButton::Cross);
	for (int32 Step = 0; Step < 4; ++Step)
	{
		Press(EPadButton::Down);
	}
	CHECK(Fight.Menus.Cursor(EBattlingMenu::Bonus) == 5);
	Press(EPadButton::Right);
	CHECK(Fight.Settings.Level == 1);
	Press(EPadButton::Circle);
	CHECK(Fight.Menus.Current == EBattlingMenu::Top);
	Press(EPadButton::Up);
	CHECK(Fight.Menus.Cursor(EBattlingMenu::Top) == 4);
	const int32 Runs = Game->Loop->ProgramRuns();
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	Game->RunUntil([&] { return Game->Loop->ProgramRuns() != Runs; }, 8);
	CHECK(Game->Loop->Program() == EGameProgram::Field);
	CHECK(Game->Session->Game().ArenaSettings() == (SettingsDefaults | 1u << 13));
	CHECK(Game->Session->Kernel().BattlingParameters[0] == 1);
}

TEST_CASE("The arena's menus start their demonstration once L2 is held on the top menu, and a button ends it", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {1, 0, 0, 0, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	const FBattlingFight& Fight = Arena->Fight();

	// Pad 1's L2 held: each pass reads the buttons the one before merged, and the 120th that finds it held starts the
	// demonstration, whose first pass runs in it.
	const FPadState L2{FPadState::Bit(EPadButton::L2)};
	int32 Passes = 0;
	while (Arena->Stage() == EBattlingGameStage::Menus && Passes < 200)
	{
		REQUIRE_FALSE(Game->Frame(L2));
		++Passes;
	}
	CHECK(Passes == 121);
	CHECK(Arena->Stage() == EBattlingGameStage::Bout);
	CHECK(Fight.State == 5);
	CHECK(Fight.Settings.Mode == EBattlingMode::Demonstration);
	CHECK(Fight.Settings.Computer == std::array<bool, 2>{true, true});
	CHECK(Fight.FireFade == 0);
	CHECK(Arena->Gears() == std::array<uint8, 2>{static_cast<uint8>(Fight.Menus.GearsRead[0]), static_cast<uint8>(Fight.Menus.GearsRead[1])});
	// Its frames: the gears alone over the frames before, two vertical blanks a pass.
	REQUIRE(Arena->LatestFrame() != nullptr);
	CHECK(Arena->LatestFrame()->bTrails);
	CHECK(Arena->LatestFrame()->Screen.empty());
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	CHECK(Fight.State == 5);
	CHECK(Game->Loop->VblanksPerFrame() == 2);
	CHECK(Arena->LatestFrame()->bTrails);
	CHECK_FALSE(Arena->LatestFrame()->Scene.Polygons.empty());

	// A button: the pass waits a vertical blank, and the next is the menus' again.
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	CHECK(Game->Loop->VblanksPerFrame() == 1);
	CHECK(Fight.Settings.Computer == std::array<bool, 2>{false, false});
	REQUIRE_FALSE(Game->Frame());
	CHECK(Arena->Stage() == EBattlingGameStage::Menus);
	CHECK(Fight.Menus.Current == EBattlingMenu::Top);
	CHECK(Fight.FireFade == 0xFF);
	CHECK_FALSE(Arena->LatestFrame()->bTrails);
}

TEST_CASE("The arena's saved settings are read from the game state's word", "[Battling]")
{
	FGameState State;
	// Never written: the defaults, written back.
	FBattlingSavedSettings Settings = FBattlingSavedSettings::Load(State);
	CHECK(Settings.WinsPerSet == 2);
	CHECK(Settings.Level == 0);
	CHECK(Settings.Vibration == std::array<bool, 2>{false, false});
	CHECK(State.ArenaSettings() == SettingsDefaults);

	// Vibration on pad 2, five wins, the hard level.
	State.SetArenaSettings(1 | 1 << 5 | 5 << 6 | 2 << 13);
	State.SetArenaDefeatedGears({0xFF, 1, 0, 0, 0, 0, 0, 0});
	Settings = FBattlingSavedSettings::Load(State);
	CHECK(Settings.WinsPerSet == 5);
	CHECK(Settings.Level == 2);
	CHECK(Settings.Vibration == std::array<bool, 2>{false, true});
	CHECK(State.ArenaDefeatedGears()[0] == 0xFF);

	// The last gear won: the gears beaten are forgotten.
	State.SetArenaSettings(State.ArenaSettings() | SettingsArgento);
	FBattlingSavedSettings::Load(State);
	CHECK(State.ArenaDefeatedGears() == std::array<uint8, 8>{});
	CHECK((State.ArenaSettings() & SettingsArgento) != 0);
}

TEST_CASE("The arena shows a controller with a motor its VIBRATION switch, and a bout's blows run the motor", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {1, 0, 0, 0, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	const FBattlingFight& Fight = Arena->Fight();
	const auto Press = [&](EPadButton Button) {
		REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(Button)}));
		REQUIRE_FALSE(Game->Frame());
	};
	// BONUS BATTLING, PLAYER1 VS COM: the gear choice. A controller without a motor has no switch there.
	Press(EPadButton::Cross);
	Press(EPadButton::Cross);
	REQUIRE(Fight.Menus.Current == EBattlingMenu::PlayerOneGear);
	CHECK(Fight.Menus.VibrationHidden == std::array<bool, 2>{true, true});
	Press(EPadButton::Down);
	CHECK(Fight.Menus.Cursor(EBattlingMenu::PlayerOneGear) == 0);

	// With one it has, on the player's side alone; off until the player switches it on.
	Game->Session->Pad().SetMotor(0, true);
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.Menus.VibrationHidden == std::array<bool, 2>{false, true});
	CHECK_FALSE(Fight.Settings.Vibration[0]);
	Press(EPadButton::Down);
	CHECK(Fight.Menus.Cursor(EBattlingMenu::PlayerOneGear) == 1);
	Press(EPadButton::Right);
	CHECK(Fight.Settings.Vibration[0]);
	Press(EPadButton::Up);

	// Both gears chosen, the versus screen, the bout; fought until a blow or a fall runs the first controller's motor.
	Press(EPadButton::Cross);
	Press(EPadButton::Cross);
	REQUIRE(Arena->Stage() == EBattlingGameStage::Versus);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	const FKernelVibration& Vibration = Game->Session->Vibration();
	int32 FramesOn = 0;
	int32 Runs = 0;
	bool bWasOn = false;
	bool bSecondOn = false;
	for (int32 Frame = 0; Frame < 6000 && Runs < 3 && Arena->Stage() != EBattlingGameStage::Menus; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame(Striking(Frame)));
		const bool bOn = Vibration.IsOn(0);
		FramesOn += bOn ? 1 : 0;
		Runs += bOn && !bWasOn ? 1 : 0;
		bWasOn = bOn;
		bSecondOn = bSecondOn || Vibration.IsOn(1);
	}
	CHECK(Runs == 3);
	// The longest, the knock-out's, lasts twenty vertical blanks: ten of the bout's passes.
	CHECK(FramesOn <= Runs * 10);
	CHECK_FALSE(bSecondOn);
}

TEST_CASE("The arena's second player chooses a gear and fights from the second controller, and pulling it pauses the bout", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {1, 0, 0, 0, 0, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	const FBattlingFight& Fight = Arena->Fight();
	// A frame with each controller's buttons, then one with none.
	const auto Press = [&](FPadState First, FPadState Second) {
		const std::array<FPadState, 1> SecondHeld{Second};
		Game->Session->SetSecondPad(SecondHeld);
		REQUIRE_FALSE(Game->Frame(First));
		Game->Session->SetSecondPad(std::array<FPadState, 1>{});
		REQUIRE_FALSE(Game->Frame());
	};
	const FPadState Cross{FPadState::Bit(EPadButton::Cross)};
	const FPadState Down{FPadState::Bit(EPadButton::Down)};
	const FPadState Right{FPadState::Bit(EPadButton::Right)};

	// BONUS BATTLING, then PLAYER1 VS PLAYER2: from here the second controller plays.
	CHECK_FALSE(Arena->ReadsSecondPad());
	Press(Cross, NoButtons);
	Press(Down, NoButtons);
	Press(Cross, NoButtons);
	REQUIRE(Fight.Menus.Current == EBattlingMenu::PlayerOneGear);
	CHECK(Fight.Settings.Mode == EBattlingMode::TwoPlayers);
	CHECK(Arena->ReadsSecondPad());

	// Each side's cursor is its own controller's: the second player steps along the roster and chooses, then the first.
	Game->Session->Pad().SetConnected(true, 1);
	const int32 SecondCursor = Fight.Menus.Gears.Cursors[1];
	Press(NoButtons, Right);
	CHECK(Fight.Menus.Gears.Cursors[1] != SecondCursor);
	CHECK(Fight.Menus.Gears.Chosen == 0);
	Press(NoButtons, Cross);
	CHECK(Fight.Menus.Gears.Chosen == 2);
	Press(Cross, NoButtons);
	CHECK(Fight.Menus.bBoutChosen);
	CHECK(Fight.Settings.Computer == std::array<bool, 2>{false, false});
	CHECK(Arena->Gears()[1] == Fight.Menus.Roster[static_cast<std::size_t>(Fight.Menus.Gears.Cursors[1])]);

	// The bout: the second controller's direction is the second gear's stick, the first gear's stays at rest.
	Press(Cross, NoButtons);
	for (int32 Frame = 0; Frame < 300 && !Fight.bFightStarted; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	REQUIRE(Fight.bFightStarted);
	const std::array<FPadState, 1> RightHeld{Right};
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Game->Session->SetSecondPad(RightHeld);
		REQUIRE_FALSE(Game->Frame());
	}
	CHECK(Fight.Fighters[1].Stick != 0);
	CHECK(Fight.Fighters[0].Stick == 0);

	// Pulled out in a bout of two players, the bout pauses.
	CHECK(Fight.State == 1);
	Game->Session->SetSecondPad(std::array<FPadState, 1>{});
	Game->Session->Pad().SetConnected(false, 1);
	REQUIRE_FALSE(Game->Frame());
	REQUIRE_FALSE(Game->Frame());
	CHECK(Fight.State == 0);
}

TEST_CASE("A bout the story starts takes the vibration switches the player last saved, and leaves the saved word alone", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	for (const bool bSwitchedOn : {false, true})
	{
		const auto Game = Testing::FGameLoopSession::Start(*Iso);
		REQUIRE(Game);
		// As the arena's menus leave the word once the first controller's switch is on; or never written.
		const uint32 Word = bSwitchedOn ? SettingsDefaults | 1u << 4 : 0;
		Game->Session->Game().SetArenaSettings(Word);
		StartArena(*Game, {0, 0, FeiGear, TinRobo, 1, 0});
		INFO(Stopped(*Game));
		const FBattlingGameProgram* Arena = Game->Loop->Battling();
		REQUIRE(Arena != nullptr);
		CHECK(Arena->Fight().Settings.Vibration == std::array<bool, 2>{bSwitchedOn, false});
		CHECK(Arena->Fight().Settings.WinsPerSet == 1);
		CHECK(Game->Session->Game().ArenaSettings() == Word);
	}
}

TEST_CASE("A gamepad's stick steers an arena bout by how far it is pushed, and the d-pad and the keyboard at full speed", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 0, FeiGear, TinRobo, 1, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	for (int32 Frame = 0; Frame < 200 && Arena->Stage() != EBattlingGameStage::Bout; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	REQUIRE(Arena->Stage() == EBattlingGameStage::Bout);
	// The round's opening, before the pads steer.
	for (int32 Frame = 0; Frame < 64; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	const FBattlingFighter& Player = Arena->Fight().Fighters[0].IsComputer() ? Arena->Fight().Fighters[1] : Arena->Fight().Fighters[0];
	REQUIRE_FALSE(Player.IsComputer());

	// Right pushed half way on the stick, which holds the direction through the bindings as well.
	FPadState Half{FPadState::Bit(EPadButton::Right)};
	Half.StickButtons = FPadState::Bit(EPadButton::Right);
	Half.StickX = 0xC0;
	Half.bAnalog = true;
	FPadState Dpad{FPadState::Bit(EPadButton::Right)};
	Dpad.bAnalog = true;
	FPadState Keyboard = Half;
	Keyboard.bAnalog = false;
	const auto Steer = [&Game, &Player](FPadState Pad)
	{
		for (int32 Frame = 0; Frame < 24; ++Frame)
		{
			REQUIRE_FALSE(Game->Frame(Pad));
		}
		return Player.Stick;
	};
	CHECK(Steer(Half) == 0x80);
	CHECK(Steer(Dpad) == 0x100);
	CHECK(Steer(Half) == 0x80);
	CHECK(Steer(Keyboard) == 0x100);
}

TEST_CASE("El-Fenrir carries the program's own part on its joint 12, in place of its own joint 13", "[Content][GameLoop][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const auto Game = Testing::FGameLoopSession::Start(*Iso);
	REQUIRE(Game);
	StartArena(*Game, {0, 0, ElFenrir, TinRobo, 1, 0});
	INFO(Stopped(*Game));
	const FBattlingGameProgram* Arena = Game->Loop->Battling();
	REQUIRE(Arena != nullptr);
	REQUIRE_FALSE(Game->Frame(FPadState{FPadState::Bit(EPadButton::Cross)}));
	for (int32 Frame = 0; Frame < 200 && Arena->Stage() != EBattlingGameStage::Bout; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	for (int32 Frame = 0; Frame < 64; ++Frame)
	{
		REQUIRE_FALSE(Game->Frame());
	}
	const FBattlingFighter& Fenrir = Arena->Fight().Fighters[0];
	REQUIRE(Fenrir.Gear == ElFenrir);
	const FBattlingSkeleton& Skeleton = Fenrir.Skeleton;
	REQUIRE(Skeleton.Joints.size() == Fenrir.File->Joints.size() + 1);
	const FBattlingJoint& Carried = Skeleton.Joints.back();
	CHECK(Carried.Parent == 12);
	CHECK(Carried.Angles == FGteVector{0, 0xC00, 0x400});
	CHECK(Carried.Translation == FGteVector{});
	CHECK(Carried.bOwnTextures);
	CHECK_FALSE(Carried.bHidden);
	REQUIRE(Carried.Part == static_cast<int16>(Skeleton.Model.Parts.size() - 1));
	CHECK(Skeleton.Model.Parts.back().Primitives.size() == 70);
	CHECK(Skeleton.Joints[13].bHidden);
	// The part goes where its parent goes: the pose turns it about joint 12's place.
	CHECK(Carried.World.T == Skeleton.Joints[12].World.T);
	// The other side's gear carries nothing of the program's.
	CHECK(Arena->Fight().Fighters[1].Skeleton.Joints.size() == Arena->Fight().Fighters[1].File->Joints.size());
}
