#include "Game/GameText.h"
#include "Game/GameTextExport.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"
#include "Text/MesCharacters.h"
#include "Text/MesText.h"
#include "Text/MessageBlocks.h"
#include "Text/MessageTableIds.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Weltall;

// Every message of the game's text, written as the export writes it and read back: its codes the disc's, byte for byte.

namespace
{
	struct FTextCensus
	{
		/** Messages by the kind of file they are written to. */
		std::array<int32, static_cast<std::size_t>(EGameTextFile::Chapters) + 1> Messages{};
		int32 Files = 0;
		int32 SizedFieldMessages = 0;
	};

	FTextCensus RoundTripText(const FIso9660& Original)
	{
		FIso9660 Disc = Original;
		Disc.Image().UseDiscAlone();
		const auto Executable = ReadBootExecutable(Disc);
		REQUIRE(Executable);
		const auto Index = FGameFileIndex::Read(*Executable);
		REQUIRE(Index);
		FTextCensus Census;
		for (const FGameTextFile File : GameTextFiles())
		{
			INFO(GameTextFilePath(File));
			const auto Tables = ReadGameText(Disc.Image(), *Index, *Executable, File);
			REQUIRE(Tables);
			if (Tables->empty())
			{
				continue;
			}
			++Census.Files;
			const std::string Text = WriteGameText(File, *Tables);
			std::vector<FMessageFileProblem> Problems;
			const std::vector<FMessageBlock> Blocks = ParseMessageBlocks(Text, Problems);
			REQUIRE(Problems.empty());
			std::size_t Block = 0;
			for (const FGameTextTable& Table : *Tables)
			{
				for (std::size_t Message = 0; Message < Table.Messages.size(); ++Message, ++Block)
				{
					REQUIRE(Block < Blocks.size());
					const std::string Id = MessageTables::MessageId(Table.Table, Table.First + static_cast<int32>(Message));
					REQUIRE(Blocks[Block].Id == Id);
					const auto Codes = MesCodesOfText(Blocks[Block].Text, FMesCharacters::Font());
					if (!Codes || *Codes != Table.Messages[Message].Codes)
					{
						FAIL(Id << " does not read back: " << Blocks[Block].Text);
					}
					if (File.Kind == EGameTextFile::Field)
					{
						const FMesExtent Extent = MesExtentOfCodes(*Codes);
						Census.SizedFieldMessages += Extent.WidthUnits == Table.Messages[Message].WidthUnits && Extent.Lines == Table.Messages[Message].Lines;
					}
				}
			}
			CHECK(Block == Blocks.size());
			Census.Messages[static_cast<std::size_t>(File.Kind)] += static_cast<int32>(Blocks.size());
		}
		return Census;
	}

	int32 Count(const FTextCensus& Census, EGameTextFile Kind)
	{
		return Census.Messages[static_cast<std::size_t>(Kind)];
	}

	void CheckTablesBesideTheField(const FTextCensus& Census)
	{
		CHECK(Count(Census, EGameTextFile::Menu) == 200);
		CHECK(Count(Census, EGameTextFile::MenuHelp) == 3827);
		CHECK(Count(Census, EGameTextFile::Battle) == 63);
		CHECK(Count(Census, EGameTextFile::BattleHelp) == 1161);
		CHECK(Count(Census, EGameTextFile::Enemies) == 4861);
		CHECK(Count(Census, EGameTextFile::WorldMap) == 369);
		CHECK(Count(Census, EGameTextFile::Battling) == 70);
		CHECK(Count(Census, EGameTextFile::Chapters) == 68);
	}
}

TEST_CASE("Every message of the first disc reads back from the text export", "[Text][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FTextCensus Census = RoundTripText(*Disc);
	CHECK(Count(Census, EGameTextFile::Field) == 18182);
	CHECK(Census.SizedFieldMessages == 17115);
	CHECK(Count(Census, EGameTextFile::Names) == 2236);
	CHECK(Count(Census, EGameTextFile::BattleEvents) == 357);
	CheckTablesBesideTheField(Census);
}

TEST_CASE("Every message of the second disc reads back from the text export", "[Text][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc2);
	const FTextCensus Census = RoundTripText(*Disc);
	CHECK(Count(Census, EGameTextFile::Field) == 6154);
	CHECK(Census.SizedFieldMessages == 5778);
	CHECK(Count(Census, EGameTextFile::Names) == 2236);
	CHECK(Count(Census, EGameTextFile::BattleEvents) == 357);
	CheckTablesBesideTheField(Census);
}

TEST_CASE("Every message of the undub's discs reads back from the text export", "[Text][Content]")
{
	REQUIRE_DISC(First, EDiscRelease::UndubDisc1);
	REQUIRE_DISC(Second, EDiscRelease::UndubDisc2);
	const FTextCensus OfFirst = RoundTripText(*First);
	CHECK(Count(OfFirst, EGameTextFile::Field) == 18182);
	CheckTablesBesideTheField(OfFirst);
	const FTextCensus OfSecond = RoundTripText(*Second);
	CHECK(Count(OfSecond, EGameTextFile::Field) == 6154);
	CheckTablesBesideTheField(OfSecond);
}

TEST_CASE("The text export writes its files below the originals' folder and keeps those there", "[Text][Content][Assets]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	const auto Folder = std::filesystem::temp_directory_path() / "WeltallTextExport";
	std::error_code Error;
	std::filesystem::remove_all(Folder, Error);
	std::filesystem::create_directories(Folder / "original" / "field", Error);
	{
		std::ofstream Kept(Folder / "original" / "field" / "002.txt", std::ios::binary);
		Kept << "mine";
	}
	auto Export = FGameTextExport::Open(*Disc, *Index, Folder, Folder / "font");
	REQUIRE(Export);
	bool bDone = false;
	int32 Steps = 0;
	while (!bDone)
	{
		const auto Stepped = (*Export)->Step();
		REQUIRE(Stepped);
		bDone = *Stepped;
		++Steps;
	}
	CHECK((*Export)->CompletedFiles() == (*Export)->TotalFiles());
	CHECK(Steps > 1);
	const auto Read = [&Folder](std::string_view Relative)
	{
		std::ifstream File(Folder / "original" / Relative, std::ios::binary);
		std::stringstream Contents;
		Contents << File.rdbuf();
		return Contents.str();
	};
	CHECK(Read("field/002.txt") == "mine");
	CHECK(Read("menu.txt").find("message menu/0\n{\nNew Game\n}\n") != std::string::npos);
	CHECK(Read("field/004.txt").find("message field/4/0  ; ") != std::string::npos);
	CHECK(Read("chapters.txt").find("message chapters/1\n{\nLahan Village\n}\n") != std::string::npos);
	const std::string Font = Read("../font/original/font.tsv");
	CHECK(Font.starts_with("code\tcell\twidth\tcharacter\tuses\n10\t0\t8\t \t"));
	CHECK(Font.find("\n8E\t126\t12\t…\t") != std::string::npos);
	CHECK(Font.find("\nFFFF\t299\t12\t\t") != std::string::npos);
	CHECK((*Export)->FontPicture().Width == 192);
	CHECK((*Export)->FontPicture().Height == 209);
	for (const std::string_view Kind : {"names.txt", "menu-help.txt", "battle.txt", "battle-help.txt", "enemies.txt", "battle-events.txt", "worldmap.txt",
			 "battling.txt"})
	{
		INFO(Kind);
		CHECK(Read(Kind).starts_with("; The game's text as the disc has it"));
	}
	std::filesystem::remove_all(Folder, Error);
}
