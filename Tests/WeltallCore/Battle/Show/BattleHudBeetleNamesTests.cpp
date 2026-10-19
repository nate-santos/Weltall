#include "Battle/Show/BattlePresentation.h"
#include "Game/BattleGameProgram.h"
#include "Game/FieldProgram.h"
#include "Game/GameState.h"
#include "Graphics/BeetleTextureNames.h"
#include "Support/GameDiscs.h"
#include "Support/GameLoopSession.h"

#include <catch_amalgamated.hpp>

#include <set>

using namespace Weltall;

// Perfect ART Works, a Beetle PSX HW pack, names the HUD's image (12/3's list, at (3C0h, 34h), 64 x 204 units) 49b52d64
// and its colourings by the CLUTs of 12/3's CLUT image: (160, 498) and (128, 498) among them.
TEST_CASE("The battle HUD's pictures lie on the upload a Beetle PSX HW pack names them by, through its palettes", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	auto Game = Testing::FGameLoopSession::Start(*Disc);
	FGameState& State = Game->Session->Game();
	State.SetParty({0, 1, 2});
	State.SetJoinedCharacters(0x1FF);
	Game->EnterField(15);
	Game->RunUntil([&] { return Game->Loop->Field()->Runtime().Context().bFieldRunning; }, 100);
	Game->Frame();
	FEventContext& Context = Game->Loop->Field()->Runtime().Context();
	Context.bBattleRequested = true;
	Context.Requests.bScriptedBattle = true;
	Context.Requests.bLeavingField = true;
	Context.Requests.BattleKind = 0;
	Context.Encounters.Formation = 1;
	Game->RunUntil([&] { return Game->Loop->Battle() != nullptr && !Game->Loop->Battle()->HudTextureRegions().empty(); }, 400);
	const FBattleGameProgram* Battle = Game->Loop->Battle();
	REQUIRE(Battle != nullptr);

	constexpr uint32 HudImage = 0x49B52D64;
	std::set<FBeetleTextureName> Names;
	int32 OnTheImage = 0;
	for (const FTextureRegion& Region : Battle->HudTextureRegions())
	{
		const std::vector<FBeetleTexturePart> Parts = BeetleTextureParts(Battle->Presentation().Show.Vram, Region);
		for (const FBeetleTexturePart& Part : Parts)
		{
			Names.insert({Part.Upload, Part.Palette});
			if (Part.Upload == HudImage)
			{
				++OnTheImage;
				CHECK((Part.UploadUnitsWide == 64 && Part.UploadUnitsHigh == 204));
			}
		}
	}
	CHECK(OnTheImage > 40);
	CHECK(Names.contains({HudImage, 0x87A54895}));
	CHECK(Names.contains({HudImage, 0xFD277244}));
}
