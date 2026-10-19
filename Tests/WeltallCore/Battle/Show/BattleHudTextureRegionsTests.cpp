#include "Battle/Show/BattlePresentation.h"
#include "Battle/Show/BattleShowHud.h"
#include "Game/BattleGameFiles.h"
#include "Game/BattleGameProgram.h"
#include "Game/FieldProgram.h"
#include "Game/GameState.h"
#include "Game/WorldMapProgram.h"
#include "Graphics/TextureRegion.h"
#include "Menu/MenuSpriteSheet.h"
#include "Support/GameDiscs.h"
#include "Support/GameLoopSession.h"
#include "WorldMap/WorldMapContext.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <set>

using namespace Weltall;

TEST_CASE("A sheet's pictures are its parts' texels, merged where they overlap or touch within one view", "[Menu]")
{
	FMenuSpriteSheet Sheet;
	FMenuSpritePart Digit{.U = 0, .V = 52, .Width = 8, .Height = 12, .Depth = 0, .ClutX = 80, .ClutY = 497, .ImageX = 0x3C0, .ImageY = 0x34};
	FMenuSpritePart NextDigit = Digit;
	NextDigit.U = 8;
	FMenuSpritePart OtherColours = Digit;
	OtherColours.ClutX = 96;
	FMenuSpritePart Apart = Digit;
	Apart.U = 100;
	FMenuSpritePart Empty = Digit;
	Empty.Width = 0;
	Sheet.SetFrame(0, {Digit});
	Sheet.SetFrame(1, {NextDigit, OtherColours});
	Sheet.SetFrame(2, {Apart, Empty, Digit});
	const std::vector<FTextureRegion> Regions = MenuSheetTextureRegions(Sheet);
	REQUIRE(Regions.size() == 3);
	const auto Find = [&Regions](uint8 U, int16 ClutX) {
		return std::find_if(Regions.begin(), Regions.end(), [U, ClutX](const FTextureRegion& Region) {
			return Region.U == U && TextureViewClut(Region.View).X == ClutX;
		});
	};
	// The two digits side by side are one picture; the same texels through other colours another; one apart a third.
	REQUIRE(Find(0, 80) != Regions.end());
	CHECK(Find(0, 80)->Width == 16);
	CHECK(Find(0, 80)->Height == 12);
	CHECK(Find(0, 96) != Regions.end());
	CHECK(Find(100, 80) != Regions.end());
}

namespace
{
	/** The keys of the battle HUD's pictures as the battle holds them: those of its portraits apart (by view) from the others. */
	struct FHudKeys
	{
		std::set<uint64> Others;
		std::set<uint64> Portraits;
	};

	FHudKeys HudKeys(const FBattleGameProgram& Battle, const FBattleHudResources& Resources)
	{
		std::set<uint32> PortraitViews;
		for (int32 Slot = 0; Slot < FBattleHud::PartySlots; ++Slot)
		{
			for (const FMenuSpritePart& Part : Resources.Sheet.Frame(FBattleHudResources::PortraitFrame + Slot))
			{
				PortraitViews.insert(PackTextureView(FTexturePage::FromAttribute(MenuGpu::TexturePage(Part.Depth, 0, Part.ImageX, Part.ImageY)),
					FClutPosition::FromAttribute(MenuGpu::Clut(Part.ClutX, Part.ClutY))));
			}
		}
		FHudKeys Keys;
		const FVram& Vram = Battle.Presentation().Show.Vram;
		for (const FTextureRegion& Region : Battle.HudTextureRegions())
		{
			(PortraitViews.contains(Region.View) ? Keys.Portraits : Keys.Others).insert(TextureRegionKey(Vram, Region));
		}
		return Keys;
	}

	/** Runs the loop until the battle's HUD has set up its pictures. */
	void RunToHud(Testing::FGameLoopSession& Game)
	{
		Game.RunUntil([&] { return Game.Loop->Battle() != nullptr && !Game.Loop->Battle()->HudTextureRegions().empty(); }, 400);
		REQUIRE(Game.Loop->Battle() != nullptr);
	}
}

// What a battle's previous program left in the HUD's page and who the party are change nothing in its pictures' keys
// but the portraits': the texture export gives every one of them, each member's portrait in any place.
TEST_CASE("The battle HUD's pictures keep their keys from battle to battle, as the texture export gives them", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	auto Forest = Testing::FGameLoopSession::Start(*Disc);
	FGameState& ForestState = Forest->Session->Game();
	ForestState.SetParty({0, 1, 2});
	ForestState.SetJoinedCharacters(0x1FF);
	Forest->EnterField(15);
	Forest->RunUntil([&] { return Forest->Loop->Field()->Runtime().Context().bFieldRunning; }, 100);
	Forest->Frame();
	FEventContext& Context = Forest->Loop->Field()->Runtime().Context();
	Context.bBattleRequested = true;
	Context.Requests.bScriptedBattle = true;
	Context.Requests.bLeavingField = true;
	Context.Requests.BattleKind = 0;
	Context.Encounters.Formation = 1;
	RunToHud(*Forest);
	const FBattleHudResources& Resources = Forest->Session->BattleFiles().HudResources;
	const FHudKeys InForest = HudKeys(*Forest->Loop->Battle(), Resources);

	auto Plains = Testing::FGameLoopSession::Start(*Disc);
	FGameState& PlainsState = Plains->Session->Game();
	PlainsState.SetParty({0, 3, 4});
	PlainsState.SetJoinedCharacters(0x1FF);
	Plains->EnterField(15);
	Plains->RunUntil([&] { return Plains->Loop->Field()->Runtime().Context().bFieldRunning; }, 100);
	Plains->Frame();
	PlainsState.SetSavedEventVariable(0, 30);
	PlainsState.SetWorldMapPlace(0x401, 1, 0xC00, 1);
	FEventContext& Leaving = Plains->Loop->Field()->Runtime().Context();
	Leaving.Requests.bWorldMap = true;
	Leaving.bFrameVariablesFrozen = true;
	Plains->RunUntil([&] { return Plains->Loop->Program() == EGameProgram::WorldMap; }, 8);
	FWorldMapProgram* WorldMap = Plains->Loop->WorldMap();
	REQUIRE(WorldMap != nullptr);
	Plains->RunUntil([&] { return !WorldMap->IsFadingOut(); }, 100);
	for (int32 Tick = 0; Tick < 64; ++Tick)
	{
		REQUIRE_FALSE(Plains->Frame());
	}
	FWorldMapEncounters& Encounters = WorldMap->Runtime().Context().Encounters;
	Encounters.Timers[0] = 1;
	Encounters.Left = FWorldMapEncounters::FootPeriod;
	Plains->RunUntil([&] { return Plains->Loop->Program() == EGameProgram::Battle; }, 16, FPadState{FPadState::Bit(EPadButton::Down)});
	RunToHud(*Plains);
	const FHudKeys OnThePlains = HudKeys(*Plains->Loop->Battle(), Resources);

	CHECK(InForest.Others.size() > 40);
	CHECK(InForest.Others == OnThePlains.Others);
	CHECK(InForest.Portraits.size() == 3);
	CHECK(InForest.Portraits != OnThePlains.Portraits);

	std::set<uint64> Exported;
	const std::optional<FError> Error = ForEachBattleHudPicture(Disc->Image(), Forest->Session->Index(), false,
		[&Exported](const FVram& Vram, const FTextureRegion& Region) { Exported.insert(TextureRegionKey(Vram, Region)); });
	REQUIRE_FALSE(Error);
	for (const FHudKeys* Keys : {&InForest, &OnThePlains})
	{
		for (const std::set<uint64>* Set : {&Keys->Others, &Keys->Portraits})
		{
			for (const uint64 Key : *Set)
			{
				INFO("key " << Key);
				CHECK(Exported.contains(Key));
			}
		}
	}
}
