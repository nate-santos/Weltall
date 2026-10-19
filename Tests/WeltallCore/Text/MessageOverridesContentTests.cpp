#include "Battle/BattleEvent.h"
#include "Battle/BattleScene.h"
#include "Battle/Show/BattleShowHud.h"
#include "Battling/BattlingData.h"
#include "Field/FieldMap.h"
#include "Foundation/LittleEndian.h"
#include "Game/ChapterTitles.h"
#include "Media/DiscAssets.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Menu/MenuResources.h"
#include "Support/GameDiscs.h"
#include "Text/MesResident.h"
#include "Text/MesText.h"
#include "Text/MessageOverrides.h"
#include "WorldMap/WorldMapScene.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <filesystem>

using namespace Weltall;

// The player's messages reach every reader of the game's text: each kind of table takes an override by its id.

namespace
{
	using FBytes = std::vector<uint8>;

	FBytes Codes(std::string_view Text)
	{
		const auto Encoded = MesCodesOfText(Text, FMesCharacters::Font());
		REQUIRE(Encoded);
		return *Encoded;
	}

	FBytes Message(std::span<const uint8> String)
	{
		const std::span<const uint8> Taken = String.first(MesMessageSize(String));
		return FBytes(Taken.begin(), Taken.end());
	}

	FBytes Message(const FMessageTable& Table, int32 Index)
	{
		return Message(Table.String(Index));
	}

	FMessageTable Parsed(std::span<const uint8> Bytes)
	{
		const auto Table = FMessageTable::Parse(Bytes);
		REQUIRE(Table);
		return *Table;
	}

	FPsxExecutable BootOf(const FIso9660& Disc)
	{
		auto Read = ReadBootExecutable(Disc);
		REQUIRE(Read);
		return std::move(*Read);
	}

	FGameFileIndex IndexOf(const FPsxExecutable& Executable)
	{
		auto Read = FGameFileIndex::Read(Executable);
		REQUIRE(Read);
		return std::move(*Read);
	}

	/** The first disc with the player's messages of Contents given by its asset store. */
	struct FOverriddenDisc
	{
		FOverriddenDisc(const FIso9660& Original, std::string_view Contents) : Disc(Original), Executable(BootOf(Disc)), Index(IndexOf(Executable))
		{
			auto Opened = FDiscAssetStore::Open(std::filesystem::temp_directory_path() / "WeltallTextOverrides", "usa.disc1", Disc.Image(), Index);
			REQUIRE(Opened);
			const std::array<FMessageFile, 1> Files = {{{"mine.txt", std::string(Contents)}}};
			(*Opened)->SetMessages(FMessageOverrides::Parse(Files, FMesCharacters::Font(), [this](const std::string& Line) { Reports.push_back(Line); }));
			(*Opened)->SetEnabled(true);
			Disc.Image().SetAssetStore(*Opened);
		}

		FIso9660 Disc;
		FPsxExecutable Executable;
		FGameFileIndex Index;
		std::vector<std::string> Reports;
	};
}

TEST_CASE("A field map's message of the player's is its table's, its window sized for it", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message field/2/2\n{\nCitan\n\"Dan, are you there!?\"\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const auto Original = LoadFieldMap(Disc->Image(), *Index, 2);
	REQUIRE(Original);
	const auto Changed = LoadFieldMap(Mine.Disc.Image(), Mine.Index, 2);
	REQUIRE(Changed);
	const FMessageTable Before = Parsed(Original->Messages);
	const FMessageTable After = Parsed(Changed->Messages);
	REQUIRE(After.Count() == Before.Count());
	CHECK(Message(Before, 2) == Codes("Citan\n\"Dan!?\""));
	CHECK(Before.WidthUnits(2) == 14);
	CHECK(Message(After, 2) == Codes("Citan\n\"Dan, are you there!?\""));
	CHECK(After.WidthUnits(2) == 44);
	CHECK(After.Lines(2) == 2);
	for (const int32 Kept : {0, 1, 3})
	{
		CHECK(Message(After, Kept) == Message(Before, Kept));
		CHECK(After.WidthUnits(Kept) == Before.WidthUnits(Kept));
	}
	CHECK(Changed->ReplacedMessages.Messages == std::vector<int32>{2});
	CHECK(Original->ReplacedMessages.Messages.empty());
	CHECK(Mine.Reports.empty());
}

TEST_CASE("The kernel's names take the player's by table and number", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message names/22/1\n{\nPotion X\n}\nmessage names/27/0\n{\nNo text\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const auto Original = LoadMesResident(Disc->Image(), *Index, *Executable);
	const auto Changed = LoadMesResident(Mine.Disc.Image(), Mine.Index, Mine.Executable);
	REQUIRE(Original);
	REQUIRE(Changed);
	CHECK(Message(Changed->NameTables.String(22, 1)) == Codes("Potion X"));
	CHECK(Message(Changed->NameTables.String(22, 0)) == Message(Original->NameTables.String(22, 0)));
	CHECK(Message(Changed->NameTables.String(17, 3)) == Message(Original->NameTables.String(17, 3)));
	for (const int32 Glyph : {0, 16, 100, 0x143})
	{
		CHECK(FBytes(Changed->NameTables.GlyphMapEntry(Glyph).begin(), Changed->NameTables.GlyphMapEntry(Glyph).end())
			== FBytes(Original->NameTables.GlyphMapEntry(Glyph).begin(), Original->NameTables.GlyphMapEntry(Glyph).end()));
	}
}

TEST_CASE("The Menu's prompts, descriptions and save places take the player's", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message menu/0\n{\nNouvelle partie\n}\nmessage menu-help/15/1\n{\nMy own help\n}\nmessage chapters/1\n{\nLahan\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const auto Original = FMenuResources::Load(Disc->Image(), *Index, *Executable);
	const auto Changed = FMenuResources::Load(Mine.Disc.Image(), Mine.Index, Mine.Executable);
	REQUIRE(Original);
	REQUIRE(Changed);
	CHECK(Message(Original->Text(), 0) == Codes("New Game"));
	CHECK(Message(Changed->Text(), 0) == Codes("Nouvelle partie"));
	CHECK(Message(Changed->Text(), 1) == Message(Original->Text(), 1));
	CHECK(Message(Changed->Text(FMenuResources::EText::ItemHelp), 1) == Codes("My own help"));
	CHECK(Message(Changed->Text(FMenuResources::EText::ItemHelp), 2) == Message(Original->Text(FMenuResources::EText::ItemHelp), 2));
	// A save's place is its chapter's title, in full-width letters filled out with full-width spaces.
	const FBytes Lahan = {0x82, 0x6B, 0x82, 0x81, 0x82, 0x88, 0x82, 0x81, 0x82, 0x8E};
	const std::span<const uint8> Place = Changed->PlaceNames().first(30);
	CHECK(FBytes(Place.begin(), Place.begin() + 10) == Lahan);
	for (std::size_t At = 10; At < 30; At += 2)
	{
		CHECK(Place[At] == 0x81);
		CHECK(Place[At + 1] == 0x40);
	}
	const std::span<const uint8> Path = Changed->PlaceNames().subspan(8 * 32, 30);
	CHECK(FBytes(Path.begin(), Path.end()) == FBytes(Original->PlaceNames().begin() + 8 * 32, Original->PlaceNames().begin() + 8 * 32 + 30));
}

TEST_CASE("A chapter's title is the player's on its card", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message chapters/2\n{\nLe chemin\n}\nmessage chapters/3\n{\nA title far longer than fifteen\n}\nmessage chapters/99\n{\nNone\n}\n");
	const auto Changed = FChapterTitles::Read(Mine.Disc.Image(), Mine.Index);
	REQUIRE(Changed);
	REQUIRE(Changed->Chapters().size() == 68);
	CHECK(Changed->Chapters()[0].Title == "Lahan Village");
	CHECK(Changed->Chapters()[1].Title == "Le chemin");
	CHECK(Changed->Chapters()[1].FirstProgress == 8);
	CHECK(Changed->Chapters()[2].Title != "A title far longer than fifteen");
	REQUIRE(Mine.Reports.size() == 2);
	CHECK(Mine.Reports[0].starts_with("chapters/3 (mine.txt line 5): a chapter's title is one line of at most 15"));
	CHECK(Mine.Reports[1] == "chapters/99 (mine.txt line 9): names no chapter: they are numbered 1 to 68");
}

TEST_CASE("The battle's messages and help lines take the player's", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message battle/1\n{\nMine\n}\nmessage battle-help/items/2\n{\nItem help\n}\nmessage battle-help/specials/0\n{\nSpecial\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const auto Original = FBattleHudResources::Load(Disc->Image(), *Index, *Executable);
	const auto Changed = FBattleHudResources::Load(Mine.Disc.Image(), Mine.Index, Mine.Executable);
	REQUIRE(Original);
	REQUIRE(Changed);
	CHECK(Message(Parsed(Changed->Messages), 1) == Codes("Mine"));
	CHECK(Message(Parsed(Changed->Messages), 2) == Message(Parsed(Original->Messages), 2));
	CHECK(Message(Parsed(Changed->ItemHelp), 2) == Codes("Item help"));
	CHECK(Message(Parsed(Changed->HelpTexts[2]), 0) == Codes("Special"));
	CHECK(Message(Parsed(Changed->HelpTexts[0]), 0) == Message(Parsed(Original->HelpTexts[0]), 0));
}

TEST_CASE("A battle scene's enemy names and messages take the player's", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message enemies/1/1\n{\nMy enemy\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const auto Original = FBattleScene::Load(Disc->Image(), *Index, 1);
	const auto Changed = FBattleScene::Load(Mine.Disc.Image(), Mine.Index, 1);
	REQUIRE(Original);
	REQUIRE(Changed);
	const auto Names = [](const FBattleScene& Scene) { return Parsed(Scene.Bytes().subspan(LittleEndian::ReadU16(Scene.Bytes(), 0x30))); };
	CHECK(Message(Names(*Changed), 1) == Codes("My enemy"));
	CHECK(Message(Names(*Changed), 2) == Message(Names(*Original), 2));
	// The table goes after the file's end: the enemies' records and scripts are where they were.
	CHECK(LittleEndian::ReadU16(Changed->Bytes(), 0x30) == Original->Bytes().size());
	const std::span<const uint8> Kept = Changed->Bytes().first(Original->Bytes().size());
	CHECK(FBytes(Kept.begin(), Kept.begin() + 0x30) == FBytes(Original->Bytes().begin(), Original->Bytes().begin() + 0x30));
	CHECK(FBytes(Kept.begin() + 0x32, Kept.end()) == FBytes(Original->Bytes().begin() + 0x32, Original->Bytes().end()));
	CHECK(Changed->ScriptBlock(0) == Original->ScriptBlock(0));
}

TEST_CASE("A scripted battle's dialogue takes the player's", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message battle-events/0/0\n{\nFei\n\"More of them?!\"\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const auto Original = FBattleEventArchive::Load(Disc->Image(), *Index);
	const auto Changed = FBattleEventArchive::Load(Mine.Disc.Image(), Mine.Index);
	REQUIRE(Original);
	REQUIRE(Changed);
	CHECK(Message(Parsed(Changed->Messages(0)), 0) == Codes("Fei\n\"More of them?!\""));
	CHECK(Message(Parsed(Changed->Messages(0)), 1) == Message(Parsed(Original->Messages(0)), 1));
	REQUIRE(Changed->Replaced(0) != nullptr);
	CHECK(Changed->Replaced(0)->Messages == std::vector<int32>{0});
	CHECK(Changed->Replaced(1)->Messages.empty());
}

TEST_CASE("The world map's place names and Battling's messages take the player's", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FOverriddenDisc Mine(*Disc, "message worldmap/0/5\n{\nForet\n}\nmessage battling/7\n{\nMy bout\n}\n");
	const auto Executable = ReadBootExecutable(*Disc);
	const auto Index = FGameFileIndex::Read(*Executable);
	const FWorldMapSceneEntry Era0{43, 16, 16, 2};
	const auto Original = ReadWorldMapScene(Disc->Image(), *Index, Era0);
	const auto Changed = ReadWorldMapScene(Mine.Disc.Image(), Mine.Index, Era0);
	REQUIRE(Original);
	REQUIRE(Changed);
	CHECK(Message(*Original->Messages(), 5) == Codes("Blackmoon Forest"));
	CHECK(Message(*Changed->Messages(), 5) == Codes("Foret"));
	CHECK(Message(*Changed->Messages(), 6) == Message(*Original->Messages(), 6));
	const auto Arena = FBattlingMessages::Read(Mine.Disc.Image(), Mine.Index);
	const auto DiscArena = FBattlingMessages::Read(Disc->Image(), *Index);
	REQUIRE(Arena);
	REQUIRE(DiscArena);
	CHECK(Message(Arena->Table(), 7) == Codes("My bout"));
	CHECK(Message(Arena->Table(), 8) == Message(DiscArena->Table(), 8));
}
