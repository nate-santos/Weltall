#include "Battle/Show/BattlePresentation.h"
#include "Battle/Show/BattleSceneGraphics.h"
#include "Battle/Show/BattleShowHud.h"
#include "Battle/Show/BattleShowMarkers.h"
#include "Battle/Show/BattleSpriteFiles.h"
#include "Battle/Show/BattleStageFiles.h"
#include "Graphics/VramStream.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"
#include "Text/MessageTable.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <vector>

using namespace Weltall;

namespace
{
	std::optional<FGameFileIndex> ReadIndex(const FIso9660& Disc)
	{
		const auto Executable = ReadBootExecutable(Disc);
		if (!Executable)
		{
			return std::nullopt;
		}
		auto Index = FGameFileIndex::Read(*Executable);
		return Index ? std::optional<FGameFileIndex>(std::move(*Index)) : std::nullopt;
	}
}

TEST_CASE("Every battle stage's model and record parse with their floors, obstacles and sequences", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const std::optional<FGameFileIndex> Index = ReadIndex(*Disc);
	REQUIRE(Index);
	const int32 Stages = FBattleStageFiles::Count(*Index);
	REQUIRE(Stages == 75);
	std::size_t Vertices = 0;
	std::size_t Triangles = 0;
	std::size_t Bones = 0;
	std::vector<int32> Obstacles;
	std::vector<int32> AnimatedStages;
	std::map<int16, int32> Kinds;
	std::map<int16, int32> Scales;
	for (int32 Stage = 0; Stage < Stages; ++Stage)
	{
		INFO("stage " << Stage);
		auto Files = FBattleStageFiles::Load(Disc->Image(), *Index, Stage);
		REQUIRE(Files);
		const FBattleStageRecord& Record = Files->Record;
		Vertices += Record.FloorVertices().size();
		Triangles += Record.FloorTriangles().size();
		Bones += Files->Model.BonePlaces.size();
		CHECK(Files->Model.BonePlaces.size() == Files->Model.Pack.Skeleton.size());
		CHECK(Record.Sequences().size() == 1);
		++Scales[Record.ObjectScale()];
		if (!Record.Obstacles().empty())
		{
			Obstacles.push_back(Stage);
		}
		// The stage object's first sequence waits for ever (op 00) unless the stage animates.
		if (Record.Bytes()[Record.Sequences()[0]] != 0)
		{
			AnimatedStages.push_back(Stage);
		}
		for (int32 Element = 0; Element < FBattleStageRecord::ElementCount; ++Element)
		{
			++Kinds[Record.Element(Element).Kind()];
		}
		CHECK(Record.CameraPreset(FBattleStageRecord::CameraPresetCount - 1));
	}
	CHECK(Vertices == 4149);
	CHECK(Triangles == 7303);
	CHECK(Bones == 506);
	CHECK(Obstacles == std::vector<int32>{6, 25});
	CHECK(AnimatedStages == std::vector<int32>{8, 18, 20});
	// No stage has a VRAM scroll (7) or asks for the clear colour by an element (5); stages 20, 34 and 36
	// hold words no kind reads, which the build passes over.
	CHECK(Kinds == std::map<int16, int32>{{0, 398}, {1, 20}, {2, 7}, {3, 15}, {207, 1}, {512, 2}, {620, 1}, {1000, 2}, {3040, 1},
		{14223, 1}, {20304, 1}, {22751, 1}});
	CHECK(Scales == std::map<int16, int32>{{4096, 75}});
}

TEST_CASE("Every scene's graphics parse: sprite enemies, image sets and models", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const std::optional<FGameFileIndex> Index = ReadIndex(*Disc);
	REQUIRE(Index);
	const FIndexRecord* Group = Index->Find(FBattleSceneGraphics::SceneDirectory, 1);
	REQUIRE(Group);
	const int32 Scenes = -Group->Size / 2;
	int32 Sprites = 0;
	int32 Models = 0;
	int32 ImageSets = 0;
	int32 Loaded = 0;
	std::vector<int32> Missing;
	for (int32 Scene = 0; Scene < Scenes; ++Scene)
	{
		INFO("scene " << Scene);
		auto Graphics = FBattleSceneGraphics::Load(Disc->Image(), *Index, Scene);
		if (!Graphics)
		{
			Missing.push_back(Scene);
			continue;
		}
		++Loaded;
		const std::span<const uint8> Bytes = Graphics->Bytes();
		for (const FBattleSceneEntry& Entry : Graphics->Entries())
		{
			if (Entry.IsSprite())
			{
				if (Entry.Data > 7)
				{
					auto File = FBattleSpriteFile::Parse(Bytes.subspan(Entry.Data, Graphics->KeptEnd() - Entry.Data));
					CHECK(File);
					++Sprites;
				}
				if (Entry.HasOwnImages())
				{
					CHECK(ReadBattleImageSet(Bytes, Entry.Images));
					++ImageSets;
				}
			}
			else if (Entry.HasOwnImages())
			{
				CHECK(ParseGearModelPack(Bytes.subspan(Entry.Images)));
				auto Motion = ParseGearMotionPack(std::vector<uint8>(Bytes.begin() + Entry.Data, Bytes.begin() + Graphics->KeptEnd()));
				CHECK(Motion);
				++Models;
			}
		}
	}
	CHECK(Scenes == 76);
	CHECK(Missing.empty());
	CHECK(Loaded == Scenes);
	CHECK(Sprites == 118);
	CHECK(ImageSets == 74);
	CHECK(Models == 118);
}

TEST_CASE("Every battle sprite file parses: the party's, their attacks, the effects and the common one", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const std::optional<FGameFileIndex> Index = ReadIndex(*Disc);
	REQUIRE(Index);
	auto Files = FBattleSpriteFiles::Load(Disc->Image(), *Index);
	REQUIRE(Files);
	std::map<EBattleSpriteSectionKind, int32> Sections;
	for (int32 Character = 0; Character < FBattleSpriteFiles::CharacterCount; ++Character)
	{
		INFO("character " << Character);
		CHECK(Files->Character(Character).Attacks == Files->Character(Character).Sprite + 17);
		auto Sprite = Files->ReadCharacterSprite(Character);
		REQUIRE(Sprite);
		auto Attacks = Files->ReadAttacks(Character);
		REQUIRE(Attacks);
		// The attack file's bank is the character's recorded voice; the sprite file has no bank.
		CHECK(Attacks->HoldsVoices());
		CHECK_FALSE(Sprite->HoldsVoices());
		for (const FBattleSpriteSection& Section : Attacks->Sections())
		{
			++Sections[Section.Kind];
		}
	}
	REQUIRE(Files->ReadCommon());
	std::vector<int32> Unreadable;
	std::map<int32, int32> Extensions;
	int32 Streams = 0;
	for (int32 Effect = 0; Effect < Files->EffectCount(); ++Effect)
	{
		INFO("effect " << Effect);
		auto File = Files->ReadEffect(Effect);
		if (!File)
		{
			Unreadable.push_back(Effect);
			continue;
		}
		++Extensions[File->Extension()];
		CHECK_FALSE(File->HoldsVoices());
		for (const FBattleSpriteSection& Section : File->Sections())
		{
			++Sections[Section.Kind];
		}
		auto Stream = Files->ReadEffectStream(Effect);
		REQUIRE(Stream);
		if (Stream->size() > 0x10)
		{
			CHECK(DecodeVramStream(*Stream));
			++Streams;
		}
	}
	CHECK(Files->EffectCount() == 251);
	CHECK(Streams == 246);
	// The build tool's stand-ins ("It's CDMAKE Dummy !"), which nothing plays.
	CHECK(Unreadable == std::vector<int32>{222, 230, 231, 234, 236});
	CHECK(Sections == std::map<EBattleSpriteSectionKind, int32>{
		{EBattleSpriteSectionKind::SoundBank, 41}, {EBattleSpriteSectionKind::SoundEffects, 202}, {EBattleSpriteSectionKind::SpriteResource, 44}});
	CHECK(Extensions == std::map<int32, int32>{{0, 136}, {2, 14}, {3, 89}, {4, 5}, {5, 1}, {6, 1}});
}

TEST_CASE("The target markers' pyramid reads from the boot executable: six corners, two flat and six gouraud triangles", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Model = FBattleMarkerModel::Read(*Executable);
	REQUIRE(Model);
	REQUIRE(Model->Vertices.size() == 6);
	REQUIRE(Model->Triangles.size() == 8);
	CHECK(Model->Vertices[0] == FGteVector{0x11, -0xF, 2});
	for (std::size_t Index = 0; Index < Model->Triangles.size(); ++Index)
	{
		const FBattleMarkerModel::FTriangle& Triangle = Model->Triangles[Index];
		const bool bFlat = Index < 2;
		CHECK(Triangle.Code == (bFlat ? 0x21 : 0x31));
		if (bFlat)
		{
			for (const std::array<uint8, 3>& Color : Triangle.Colors)
			{
				CHECK(Color == std::array<uint8, 3>{0xFF, 0, 0});
			}
		}
	}
	CHECK(Model->Triangles[2].Colors[0] == std::array<uint8, 3>{0xC1, 0xB6, 0x62});
	CHECK(Model->Triangles[2].Vertices == std::array<uint16, 3>{4, 5, 0});
}

TEST_CASE("Each battle kind's start sounds read from the boot executable, and their sound file from the disc", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Sounds = FBattleStartSounds::Read(*Executable);
	REQUIRE(Sounds);
	constexpr uint8 None = FBattleStartSounds::None;
	using FKind = std::array<uint8, FBattleStartSounds::PerKind>;
	CHECK(Sounds->Kinds[0] == FKind{1, 2, None});
	CHECK(Sounds->Kinds[1] == FKind{3, 4, None});
	CHECK(Sounds->Kinds[2] == FKind{5, 6, 7});
	CHECK(Sounds->Kinds[3] == FKind{8, 9, 10});
	CHECK(Sounds->Kinds[4] == FKind{1, 2, None});
	CHECK(Sounds->Kinds[5] == FKind{1, 2, None});
	const auto Index = ReadIndex(*Disc);
	REQUIRE(Index);
	const auto Files = FBattleSpriteFiles::Load(Disc->Image(), *Index);
	REQUIRE(Files);
	const auto Seds = Files->ReadStartSounds();
	REQUIRE(Seds);
	CHECK(Seds->size() > 0x16);
}

TEST_CASE("The result screen's sheet, images and portraits are the result file's, its label tables the Battle program's", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = ReadIndex(*Disc);
	REQUIRE(Index);
	const auto Resources = FBattleHudResources::Load(Disc->Image(), *Index, *Executable);
	REQUIRE(Resources);
	// The sheet's digits are its first frames, the party's portraits three of its last; a portrait a character and one more.
	CHECK(Resources->ResultSheet.FrameCount() == 262);
	CHECK(Resources->ResultSheet.Frame(0).size() == 1);
	CHECK(Resources->ResultSheet.Frame(0xFC).size() == 1);
	CHECK_FALSE(Resources->ResultImages.empty());
	CHECK(Resources->ResultPortraits.size() >= 12 * 0x460);

	// A panel's labels: a slash between HP and its most, and between EP and its most.
	const FBattleHudTables& Tables = Resources->Tables;
	constexpr uint8 Slash = 0x3E;
	const auto Has = [](const auto& Labels, uint8 Frame, int16 X, int16 Y) {
		return std::any_of(Labels.begin(), Labels.end(), [=](const FBattleHudResultLabel& Label) { return Label.Frame == Frame && Label.X == X && Label.Y == Y; });
	};
	CHECK(Has(Tables.ResultPanelLabels, Slash, 0x60, 0x20));
	CHECK(Has(Tables.ResultPanelLabels, Slash, 0x60, 0x28));
	// The gold's: a "G" after the gold gained and after the party's.
	const uint8 Gold = Tables.ResultGoldLabels[0].Frame;
	CHECK(std::count_if(Tables.ResultGoldLabels.begin(), Tables.ResultGoldLabels.end(), [](const FBattleHudResultLabel& Label) { return Label.X == 0x110; }) == 2);
	CHECK(Has(Tables.ResultGoldLabels, Gold, 0x110, 0x50));
	CHECK(Has(Tables.ResultGoldLabels, Gold, 0x110, 0x60));
	// The level-up window's: "Level Up!" in red on the first row, the stats' names down the rows, every other one dim.
	constexpr std::array<uint8, 7> StatNames{0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE4, 0xEB};
	int32 Red = 0;
	for (const FBattleHudLevelLabel& Label : Tables.ResultLevelLabels)
	{
		Red += Label.Frame != FBattleHudLevelLabel::None && Label.bTinted && Label.Tint == 0;
		CHECK((!Label.bTinted || Label.Tint <= 1));
		const auto Stat = std::find(StatNames.begin(), StatNames.end(), Label.Frame);
		if (Stat != StatNames.end())
		{
			const auto Row = Stat - StatNames.begin();
			CHECK(Label.X == 0x58);
			CHECK(Label.Y == 0x90 + 8 * Row);
			CHECK(Label.bTinted == (Row == 1 || Row == 3 || Row == 5));
		}
	}
	CHECK(Red > 0);
}

TEST_CASE("The battle's messages are the data file's section 26h, a table of the actions' names", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = ReadIndex(*Disc);
	REQUIRE(Index);
	const auto Resources = FBattleHudResources::Load(Disc->Image(), *Index, *Executable);
	REQUIRE(Resources);
	const auto Messages = FMessageTable::Parse(Resources->Messages);
	REQUIRE(Messages);
	CHECK(Messages->Count() == 0x3F);
	CHECK_FALSE(Messages->String(1).empty());
}
