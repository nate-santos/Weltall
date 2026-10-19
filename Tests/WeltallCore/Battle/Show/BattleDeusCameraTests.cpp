#include "Battle/BattleRuntime.h"
#include "Battle/Show/BattleGameHud.h"
#include "Battle/Show/BattlePresentation.h"
#include "Battle/Show/BattleShowCamera.h"
#include "Game/GameState.h"
#include "Game/GameStateNames.h"
#include "Game/KernelState.h"
#include "Game/PsxRandom.h"
#include "Input/KernelPad.h"
#include "Support/BattleProgramDisc.h"
#include "Support/BattleSoundRecorder.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <memory>
#include <optional>

using namespace Weltall;

namespace
{
	/** Deus's first fight (disc 2's map 198, formation 12): Fei, Elly and Billy in their gears, Cross pressed every 24 frames. */
	constexpr int32 Map = 198;
	constexpr int32 Formation = 12;
	/** The draws that bring his attack on one gear (command 11h) about 45 seconds in. */
	constexpr uint32 Seed = 4;
	constexpr uint8 OneGearAttack = 0x11;

	/** How Deus's first attack on one gear played: the frame it began, and the frames its turn's playback took (none: it never ended). */
	struct FDeusAttack
	{
		std::optional<int32> Began;
		std::optional<int32> Took;
	};

	/** The fight under Rules until the attack has played or Limit frames have passed, the eye moved Away along x as it begins. */
	FDeusAttack PlayDeus(const Testing::FBattleProgramDisc& Loaded, const FBattleRuleOptions& Rules, int16 Away, int32 Limit)
	{
		FGameState Game = *Loaded.NewGame;
		Game.SetJoinedCharacters(0x1FF);
		Game.SetParty({0, 1, 4});
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			Game.SetPartySlotInGear(Slot, 1);
		}
		FPsxRandom Random(Seed);
		FKernelPad Pad;
		FGameStateNames Names(Game);
		FMesContext Text;
		Text.Font = &Loaded.Resident.Font;
		Text.Tables = &Loaded.Resident.NameTables;
		Text.State = &Names;
		Text.ButtonMap = MesKernelButtonMap;
		FKernelState Kernel;
		Kernel.InitNewGame();
		const FBattleHudSettings Settings{Kernel.WindowColour, Kernel.BattleWindowBlend};

		FBattleRequest Request;
		const std::vector<uint8>& Encounters = Loaded.Encounters.at(Map);
		std::copy_n(Encounters.begin() + Formation * static_cast<std::ptrdiff_t>(FBattleFormation::RecordSize), FBattleFormation::RecordSize,
			Request.Formation.begin());
		Request.RuleOptions = Rules;
		Testing::FBattleSoundRecorder Sound;
		FBattleGameHud Hud(*Loaded.HudResources, Text, Settings);
		const auto Presentation = std::make_unique<FBattlePresentation>(Loaded.PresentationFiles(), &Sound, &Hud);
		FBattleRuntimeServices Services;
		Services.Image = &Loaded.Disc->Image();
		Services.Index = &*Loaded.Index;
		Services.Game = &Game;
		Services.Random = &Random;
		Services.Pad = &Pad;
		Services.Show = Presentation.get();
		Services.Tables = &*Loaded.Tables;
		Services.Program = &*Loaded.Program;
		Services.Growth = &*Loaded.Growth;
		Services.Events = &*Loaded.Events;
		Services.Arctangents = &*Loaded.Arctangents;
		auto Runtime = FBattleRuntime::Begin(Request, Services);
		REQUIRE(Runtime);

		FDeusAttack Attack;
		for (int32 Frame = 0; Frame < Limit && !(*Runtime)->IsOver(); ++Frame)
		{
			for (int32 Count = (*Runtime)->VblanksPerFrame(); Count > 0; --Count)
			{
				const bool bPressed = Frame % 24 < 2;
				Pad.Vblank(FPadState{static_cast<uint16>(bPressed ? 1u << 14 : 0u)}, Game.PlayTime());
			}
			(*Runtime)->Frame();
			REQUIRE((*Runtime)->Fault() == EBattleFault::None);
			// The rules wait while the show plays an action: the next one begins only once the attack has played.
			const FBattleContext& Context = (*Runtime)->Context();
			const bool bAttacking = Context.Action.ActorSlot >= FBattleContext::PartySlotCount && Context.Action.Command == OneGearAttack;
			if (!Attack.Began && bAttacking)
			{
				Attack.Began = Frame;
				FBattleCamera& Camera = Presentation->Show.Camera;
				Camera.Eye[0] = static_cast<int16>(Camera.Eye[0] + Away);
			}
			else if (Attack.Began && !bAttacking)
			{
				Attack.Took = Frame - *Attack.Began;
				break;
			}
		}
		return Attack;
	}
}

TEST_CASE("Deus's attack on one gear waits forever for a camera far from its goal, as the original does, and plays on with the fix", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc2);
	constexpr std::array<int32, 1> Maps{Map};
	Testing::FBattleProgramDisc Loaded;
	REQUIRE(Testing::LoadBattleProgramDisc(*Disc, Maps, Loaded));
	// From the view the fight leaves, the attack plays in about fifteen seconds; from 3000 units round (a side view), the
	// model camera holds the eye before the effect's framing reaches its goal, and the effect waits for it forever.
	constexpr int16 SideView = 3000;
	constexpr int32 Limit = 4500;
	FBattleRuleOptions Kept = FBattleRuleOptions::Original();
	Kept.bDeusKeepsCamera = true;
	const FDeusAttack Plain = PlayDeus(Loaded, FBattleRuleOptions::Original(), 0, Limit);
	REQUIRE(Plain.Began);
	REQUIRE(Plain.Took);
	const FDeusAttack Held = PlayDeus(Loaded, FBattleRuleOptions::Original(), SideView, Limit);
	CHECK(Held.Began == Plain.Began);
	CHECK_FALSE(Held.Took);
	const FDeusAttack Fixed = PlayDeus(Loaded, Kept, SideView, Limit);
	CHECK(Fixed.Began == Plain.Began);
	CHECK(Fixed.Took == Plain.Took);
}
