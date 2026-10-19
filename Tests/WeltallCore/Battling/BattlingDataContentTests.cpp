#include "Battling/BattlingArenaData.h"
#include "Battling/BattlingData.h"
#include "Battling/BattlingLabels.h"
#include "Battling/BattlingMenus.h"
#include "Battling/BattlingSkeleton.h"
#include "Formats/Lzss.h"
#include "Graphics/MdlModel.h"
#include "Graphics/Texture.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace Weltall;

// Battling's data on both discs: the program's tables, the gear table, the arena's
// heightfield and every gear file, with the references the fight follows checked to stay inside what they point into.
namespace
{
	struct FOpened
	{
		std::unique_ptr<FPsxExecutable> Executable;
		std::unique_ptr<FGameFileIndex> Index;
	};

	/** The labels' texts are the program's strings: each where the program keeps it, with what its format adds after it. */
	void CheckLabelTexts(const FIso9660& Disc, const FGameFileIndex& Index)
	{
		const FIndexRecord* Record = Index.Find(1, 13);
		REQUIRE(Record != nullptr);
		const auto Sectors = ReadFileSectors(Disc.Image(), *Record);
		REQUIRE(Sectors);
		const auto Program = UnpackLzss(*Sectors);
		REQUIRE(Program);
		const auto StringAt = [&](std::size_t Offset) {
			REQUIRE(Offset < Program->size());
			const auto First = Program->begin() + static_cast<std::ptrdiff_t>(Offset);
			return std::string(First, std::find(First, Program->end(), uint8{0}));
		};
		using namespace BattlingLabelTexts;
		struct FText
		{
			std::string_view Text;
			std::size_t Offset;
			std::string_view Format;
		};
		const std::array<FText, 23> Texts{{
			{Ether, 0x120, ""}, {DrawGame, 0x14C, ""}, {Fight, 0x174, ""}, {Start, 0x17C, ""},
			{RubberBandMode, 0x188, ""}, {Ready, 0x19C, ""}, {RubberBandBattle, 0x1A4, ""}, {Practice, 0x1B8, ""},
			{Round, 0x1C4, "%d"}, {Strength, 0x218, "%d"}, {Frames, 0x220, "%d-%d"}, {Replay, 0x25C, ""},
			{YouWereVictorious, 0x284, ""}, {PlayerOneVictory, 0x298, ""}, {ComputerOneVictory, 0x2A8, ""},
			{YouWereDefeated, 0x2B8, ""}, {By, 0x2CC, "%s"}, {PlayerTwoVictory, 0x2D8, ""}, {ComputerTwoVictory, 0x2E8, ""},
			{Winner, 0xD8, ""}, {Level, 0xE0, ""}, {Matches, 0xE8, ""}, {Time, 0xF0, ""},
		}};
		for (const FText& Text : Texts)
		{
			CHECK(StringAt(Text.Offset) == std::string(Text.Text) + std::string(Text.Format));
		}
		CHECK(StringAt(0xFC) == "%d/%d" + std::string(Versus) + "%s");
		CHECK(StringAt(0x6C0) == "%02d" + std::string(Minutes) + "%02d" + std::string(Seconds) + "%02d");
		// The moves' and the levels' names, through the program's tables of pointers to them.
		constexpr uint32 ProgramAddress = 0x8006FAF0;
		const auto CheckNames = [&](std::size_t Table, std::span<const std::string_view> Names) {
			for (std::size_t Name = 0; Name < Names.size(); ++Name)
			{
				const std::size_t At = Table + 4 * Name;
				const uint32 Pointer = static_cast<uint32>((*Program)[At] | (*Program)[At + 1] << 8 | (*Program)[At + 2] << 16 | (*Program)[At + 3] << 24);
				CHECK(StringAt(Pointer - ProgramAddress) == Names[Name]);
			}
		};
		CheckNames(0x216A8, MoveNames);
		CheckNames(0x21804, LevelNames);
		CheckNames(0x2183C, PracticeStyles);
		for (std::size_t Rate = 0; Rate < PracticeFrameRates.size(); ++Rate)
		{
			CHECK((*Program)[0x21810 + Rate] == PracticeFrameRates[Rate]);
		}
		CHECK(StringAt(0x470) == "%d" + std::string(FramesPerSecond));
		// The pause's menus' labels, through the program's menus (3Ch bytes each, their items at +4) and items (14h bytes, the
		// label at +4).
		const auto Word = [&](std::size_t At) {
			return static_cast<uint32>((*Program)[At] | (*Program)[At + 1] << 8 | (*Program)[At + 2] << 16 | (*Program)[At + 3] << 24);
		};
		constexpr std::size_t Menus = 0x800915AC - ProgramAddress;
		for (const EBattlingMenu Menu : {EBattlingMenu::Pause, EBattlingMenu::GiveUp, EBattlingMenu::PracticePause})
		{
			const FBattlingMenuDefinition& Definition = BattlingMenuDefinition(Menu);
			const std::size_t MenuAt = Menus + 0x3C * static_cast<std::size_t>(Menu);
			CHECK((*Program)[MenuAt] == Definition.Layout);
			for (std::size_t Item = 0; Item < Definition.Items.size(); ++Item)
			{
				const uint32 Label = Word(Word(MenuAt + 4) - ProgramAddress + 0x14 * Item + 4);
				CHECK(StringAt(Label - ProgramAddress) == Definition.Items[Item].Label);
			}
		}
	}

	FOpened Open(const FIso9660& Disc)
	{
		FOpened Opened;
		auto Executable = ReadBootExecutable(Disc);
		REQUIRE(Executable);
		Opened.Executable = std::make_unique<FPsxExecutable>(std::move(*Executable));
		auto Index = FGameFileIndex::Read(*Opened.Executable);
		REQUIRE(Index);
		Opened.Index = std::make_unique<FGameFileIndex>(std::move(*Index));
		return Opened;
	}

	/** Every keyed channel of an animation decoded for all its frames, as the program steps it: true when no byte lies past the file. */
	bool StreamsStayInside(const FBattlingGearFile& File, const FBattlingAnimation& Animation)
	{
		for (const FBattlingChannel& Channel : Animation.KeyedChannels)
		{
			uint32 At = Channel.Value;
			int32 Hold = 0;
			for (int32 Frame = 0; Frame < Animation.Frames; ++Frame)
			{
				if (Hold != 0)
				{
					--Hold;
					continue;
				}
				if (At >= File.Bytes.size())
				{
					return false;
				}
				const uint8 Code = File.Bytes[At++];
				if ((Code & 0xC0) == 0x80)
				{
					Hold = Code & 0x3F;
				}
				else if ((Code & 0xC0) == 0xC0)
				{
					if (At >= File.Bytes.size())
					{
						return false;
					}
					++At;
				}
			}
		}
		return true;
	}

	/** Whether a joint's vertex lies in its part's list; where it does not, whether the bytes the program reads lie in the file. */
	bool VertexInside(const FBattlingGearFile& File, const FMdlModel& Model, uint8 Joint, int16 Vertex, bool bInFileOnly)
	{
		if (Joint >= File.Joints.size())
		{
			return false;
		}
		const int16 Part = File.Joints[Joint].Part;
		if (Vertex == 0 || Part < 0)
		{
			return true;
		}
		if (!bInFileOnly)
		{
			return Part < static_cast<int16>(Model.Parts.size()) && Vertex > 0
				&& Vertex <= static_cast<int16>(Model.Parts[static_cast<std::size_t>(Part)].Vertices.size());
		}
		FBattlingSkeleton Skeleton;
		Skeleton.Build(File, Model);
		const int64 At = static_cast<int64>(Skeleton.PartVertices[static_cast<std::size_t>(Part)]) + (Vertex - 1) * 8;
		return At >= 0 && At + 6 <= static_cast<int64>(File.Bytes.size());
	}

	/** The two events whose vertex lies past its part's list: Silverstar's BBA swing (vertex -1) and Etone 1's victory spark (109 of 108). */
	bool KnownStrayVertex(int32 Gear, const FBattlingEvent& Event)
	{
		return (Gear == 21 && Event.Kind == EBattlingEventKind::Attack && Event.VertexB == -1) || (Gear == 30 && Event.Kind == EBattlingEventKind::Effect && Event.VertexA == 109);
	}
}

TEST_CASE("Battling's gear files hold what the fight reads on both discs", "[Content][Battling]")
{
	for (const EDiscRelease Release : {EDiscRelease::UsaDisc1, EDiscRelease::UsaDisc2})
	{
		const FIso9660* Disc = Weltall::Testing::FindDisc(Release);
		if (Disc == nullptr)
		{
			continue;
		}
		const FOpened Opened = Open(*Disc);
		auto Tables = FBattlingProgramTables::Read(Disc->Image(), *Opened.Index, *Opened.Executable, Release);
		REQUIRE(Tables);
		CHECK(Tables->Chain[0] == std::array<uint8, 2>{1, 2});
		CHECK(Tables->Chain[6] == std::array<uint8, 2>{13, 14});
		CHECK(Tables->Chain[7] == std::array<uint8, 2>{0, 0});
		CHECK(Tables->AnimationKinds[8].Kind == 0);
		CHECK(Tables->AnimationKinds[8].Next == 9);
		CHECK(Tables->AnimationKinds[9].Kind == 2);
		CHECK(Tables->AnimationKinds[9].Next == 10);
		CHECK(Tables->AnimationKinds[32].Kind == 0);
		CHECK(Tables->AnimationKinds[32].Next == 0xFF);
		CHECK(Tables->ProjectileKinds[0].Homing == 0x600);
		CHECK(Tables->ProjectileKinds[0].Life == 45);
		CHECK(Tables->ProjectileKinds[0].Sound == 0xB);
		CHECK(Tables->TrailLengths == std::array<int16, 3>{0x18, 0x30, 0x10});
		CHECK(Tables->ChainInputs[14] == std::array<uint8, 3>{2, 2, 2});
		CHECK(Tables->GearNames[0] == "WELTALL");
		CHECK(Tables->GearNames[17] == "GANADOR");
		CHECK(Tables->GearNames[28] == "TIN ROBO");
		CHECK(Tables->KnockOutLabel == "KNOCK OUT!!");
		CHECK(Tables->FontGlyphs[0].U == 60);
		CHECK(Tables->FontGlyphs[10].Height == 18);
		CHECK(Tables->FontGlyphs[41].Width == 8);
		// El-Fenrir's part of the program's own: 41 corners and 70 triangles on the arena's own page and CLUT.
		CHECK(Tables->ElFenrirPart.Vertices.size() == 41);
		REQUIRE(Tables->ElFenrirPart.Primitives.size() == 70);
		CHECK(Tables->ElFenrirPart.Primitives[0].Type == EMdlPrimitiveType::TexturedTriangle);
		CHECK(Tables->ElFenrirPart.Primitives[0].Texture({}) == FMdlTextureState{0x1E, 0x6F7F});
		CheckLabelTexts(*Disc, *Opened.Index);

		auto GearTable = FBattlingGearTable::Read(Disc->Image(), *Opened.Index);
		REQUIRE(GearTable);
		CHECK(GearTable->Gears[0].Power == 100);
		CHECK(GearTable->Gears[22].Threshold == 999);

		auto Terrain = FBattlingTerrain::Read(Disc->Image(), *Opened.Index);
		REQUIRE(Terrain);
		std::array<int32, 4> Grounds{};
		for (int32 Cell = 0; Cell < FBattlingTerrain::Size * FBattlingTerrain::Size; ++Cell)
		{
			++Grounds[(Terrain->CellAt(Cell) >> 24) & 3];
		}
		CHECK(Grounds == std::array<int32, 4>{643, 4078, 9548, 2115});

		// What the HUD draws with: the gauge's points and the names' palette in the program, the tubes and the heat in
		// the archive, the tubes' palette as the HUD's load changes it, the heat's own never uploaded.
		auto ArenaTables = FBattlingArenaTables::Read(Disc->Image(), *Opened.Index, Release);
		REQUIRE(ArenaTables);
		CHECK(ArenaTables->HudPoints == std::array<std::array<int16, 2>, 8>{{{0, 1}, {0x7F, 1}, {0x77, 0x10}, {0x40, 0x10}, {0x34, 8}, {-4, 8},
			{0x48, 1}, {0x38, 1}}});
		CHECK(ArenaTables->NamePalette[0] == 0x8000);
		CHECK(ArenaTables->NamePalette[15] == 0x8000);
		auto ArenaTextures = FBattlingArenaTextures::Read(Disc->Image(), *Opened.Index);
		REQUIRE(ArenaTextures);
		CHECK(ArenaTextures->HudTubes == FVramRect{0x3E8, 0x18E, 6, 97});
		CHECK(ArenaTextures->HudHeat == FVramRect{0x394, 0x118, 3, 64});
		CHECK(ArenaTextures->HudClut == 0x6DFF);
		bool bTubesPalette = false;
		for (const FVramImage& Upload : ArenaTextures->Uploads)
		{
			CHECK_FALSE(Upload.Rect == FVramRect{0x93, 0x139, 16, 1});
			if (Upload.Rect == FVramRect{0x3F0, 0x1B7, 16, 1})
			{
				bTubesPalette = true;
				CHECK(Upload.Units[0] == 0);
				CHECK(Upload.Units[1] == 0x8000);
			}
		}
		CHECK(bTubesPalette);

		for (int32 Gear = 0; Gear < BattlingGears::Count; ++Gear)
		{
			INFO("gear " << Gear);
			auto File = FBattlingGearFile::Read(Disc->Image(), *Opened.Index, Gear);
			REQUIRE(File);
			auto Model = ParseMdlModel(std::span<const uint8>(File->Bytes).subspan(File->Model));
			REQUIRE(Model);
			CHECK(VertexInside(*File, *Model, File->TopJoint, File->TopVertex, false));
			CHECK(VertexInside(*File, *Model, File->FootJoints[0], File->FootVertices[0], false));
			CHECK(VertexInside(*File, *Model, File->FootJoints[1], File->FootVertices[1], false));
			for (std::size_t Number = 0; Number < File->Animations.size(); ++Number)
			{
				if (File->HasAnimation[Number])
				{
					CHECK(StreamsStayInside(*File, File->Animations[Number]));
				}
			}
			for (const FBattlingEvent& Event : File->Events)
			{
				switch (Event.Kind)
				{
				case EBattlingEventKind::Attack:
				case EBattlingEventKind::Effect:
				{
					const bool bStray = KnownStrayVertex(Gear, Event);
					CHECK(VertexInside(*File, *Model, Event.JointA, Event.VertexA, bStray));
					CHECK(VertexInside(*File, *Model, Event.JointB, Event.VertexB, bStray));
				}
					break;
				case EBattlingEventKind::HidePart:
				case EBattlingEventKind::ShowPart:
					REQUIRE(Event.What < File->Joints.size());
					CHECK(File->Joints[Event.What].Part >= 0);
					break;
				default:
					break;
				}
			}
			// Every animation a fighter can start is there (an absent one would have the program read its channel counts from
			// the bottom of memory): the eighteen of the program's own and each move's, 12h on.
			std::vector<int32> Started;
			for (int32 Animation = 0; Animation < 0x12; ++Animation)
			{
				Started.push_back(Animation);
			}
			for (const FBattlingMove& Move : File->Moves)
			{
				if (Move.Animation != 0)
				{
					Started.push_back(0x12 + Move.Animation);
				}
			}
			for (const int32 Animation : Started)
			{
				REQUIRE(Animation < FBattlingProgramTables::AnimationCount);
				REQUIRE(static_cast<std::size_t>(Animation) < File->Animations.size());
				REQUIRE(static_cast<std::size_t>(Animation) < File->AnimationTable.size());
				CHECK(File->HasAnimation[static_cast<std::size_t>(Animation)]);
			}
		}
	}
}

TEST_CASE("Battling's scripts and their messages read on both discs", "[Content][Battling]")
{
	for (const EDiscRelease Release : {EDiscRelease::UsaDisc1, EDiscRelease::UsaDisc2})
	{
		const FIso9660* Disc = Weltall::Testing::FindDisc(Release);
		if (Disc == nullptr)
		{
			continue;
		}
		const FOpened Opened = Open(*Disc);
		// The reader refuses scripts the machine would hang on or that drive a fighter none of them chose.
		auto Tables = FBattlingProgramTables::Read(Disc->Image(), *Opened.Index, *Opened.Executable, Release);
		REQUIRE(Tables);
		CHECK(Tables->Lessons == std::array<uint16, FBattlingProgramTables::LessonCount>{0x34, 0x08, 0x0C, 0x74, 0xA8, 0x38, 0xD8, 0x104, 0x11C, 0x04});
		CHECK(Tables->EndingScript == 0x18C);
		CHECK(Tables->SettleSteps[0] == std::array<int8, 2>{-1, -1});
		CHECK(Tables->SettleSteps[7] == std::array<int8, 2>{1, 1});
		auto Messages = FBattlingMessages::Read(Disc->Image(), *Opened.Index);
		REQUIRE(Messages);
		// Every message a script opens, the lesson menu's list and Leonard's two lines.
		for (std::size_t At = 0; At + 1 < Tables->Scripts.size(); ++At)
		{
			if (Tables->Scripts[At] == 0x12 && Tables->Scripts[At + 1] >= 0x2B && Tables->Scripts[At + 1] <= 0x45)
			{
				CHECK(Messages->Table().String(Tables->Scripts[At + 1]).front() != 0);
			}
		}
		for (const int32 Message : {0x42, 0x43, 0x44})
		{
			CHECK(Messages->Table().String(Message).front() != 0);
		}
	}
}

TEST_CASE("Each side's trails read their own gear's colour from the palette made of both", "[Content][Battling]")
{
	REQUIRE_DISC(Iso, EDiscRelease::UsaDisc1);
	const FOpened Opened = Open(*Iso);
	const auto Textures = FBattlingArenaTextures::Read(Iso->Image(), *Opened.Index);
	REQUIRE(Textures);
	const FVramImage Palette = BattlingTrailPaletteUpload(*Textures, {0xFF, 0x80, 0x7F}, {0x90, 0xA0, 0xC4});
	// Each channel above the middle, to five bits, and the blend bit.
	REQUIRE(Palette.Units.size() == 4);
	CHECK(Palette.Units[0] == 0x801F);
	CHECK(Palette.Units[1] == (0x8000 | 0x11 << 10 | 8 << 5 | 4));
	const FClutPosition Clut = FClutPosition::FromAttribute(Textures->TrailClut);
	CHECK(Clut.X == Palette.Rect.X);
	CHECK(Clut.Y == Palette.Rect.Y);
	// The page's 4-bit texel each side reads lies in the palette's own row and picks that side's colour.
	const FTexturePage Page = FTexturePage::FromAttribute(Textures->TrailPage);
	REQUIRE(Page.Depth == ETextureDepth::Indexed4);
	for (std::size_t Side = 0; Side < 2; ++Side)
	{
		const uint8 U = static_cast<uint8>(Textures->TrailTexels[Side]);
		const uint8 V = static_cast<uint8>(Textures->TrailTexels[Side] >> 8);
		CHECK(Page.Y + V == Palette.Rect.Y);
		const int32 Unit = Page.X + U / 4 - Palette.Rect.X;
		REQUIRE(Unit >= 0);
		REQUIRE(Unit < static_cast<int32>(Palette.Units.size()));
		CHECK((Palette.Units[static_cast<std::size_t>(Unit)] >> (4 * (U % 4)) & 0xF) == Side);
	}
}
