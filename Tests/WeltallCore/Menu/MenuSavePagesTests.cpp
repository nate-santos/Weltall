#include "Menu/MenuSavePages.h"
#include "Game/GameState.h"
#include "Game/SaveStore.h"
#include "Game/SaveDocuments.h"
#include "Game/SaveWrite.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Menu/MenuFileBrowser.h"
#include "Menu/MenuSaveCards.h"
#include "Menu/MenuResources.h"
#include "Menu/OptionsText.h"
#include "Support/GameDiscs.h"
#include "Support/GameText.h"
#include "Text/MesGlyphs.h"

#include <catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>

using namespace Weltall;

// The options screen's saves pages: browsing for a card image, its saves copied into a card, a card written out.
namespace
{
	/** A folder of the test's own under the system's temporary folder, empty at the start and gone at the end. */
	struct FScratchFolder
	{
		std::filesystem::path Path;

		explicit FScratchFolder(const char* Name) : Path(std::filesystem::temp_directory_path() / Name)
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
			std::filesystem::create_directories(Path);
		}

		~FScratchFolder()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}
	};

	void Touch(const std::filesystem::path& File, std::string_view Bytes = "x")
	{
		std::ofstream(File, std::ios::binary) << Bytes;
	}

	std::vector<std::string> Names(const FFileBrowser& Browser)
	{
		std::vector<std::string> Listed;
		for (const FBrowserEntry& Entry : Browser.Entries())
		{
			Listed.push_back(Entry.Name);
		}
		return Listed;
	}

	namespace Text = OptionsText::Saves;

	/** Every row's label and value side by side in the list, every help line in its strip, in the font's widths and letters. */
	bool AllFit(const std::vector<FSettingsRow>& Rows, const FMesFont& Font)
	{
		bool bInFont = true;
		const auto Width = [&Font, &bInFont](std::string_view Words)
		{
			int32 Units = 0;
			for (const char Character : Words)
			{
				const std::optional<FMesCode> Code = MesCodeOfCharacter(Character);
				bInFont = bInFont && Code.has_value();
				Units += Font.WidthUnits(Code.value_or(FMesCode{}));
			}
			return Units * 4;
		};
		for (const FSettingsRow& Row : Rows)
		{
			if (0x28 + Width(Row.Label) + 8 > 0x128 - Width(Row.Value))
			{
				return false;
			}
			for (const std::string& Line : Row.Help)
			{
				if (Width(Line) > 0x3D * 4)
				{
					return false;
				}
			}
		}
		return bInFont;
	}

	int32 RowNamed(const std::vector<FSettingsRow>& Rows, std::string_view Label)
	{
		for (std::size_t Row = 0; Row < Rows.size(); ++Row)
		{
			if (Rows[Row].Label == Label)
			{
				return static_cast<int32>(Row);
			}
		}
		return -1;
	}
}

TEST_CASE("The file browser lists a folder's folders, then the card images and saves in it, by name", "[Menu]")
{
	FScratchFolder Scratch("WeltallFileBrowser");
	std::filesystem::create_directories(Scratch.Path / "b");
	std::filesystem::create_directories(Scratch.Path / "A");
	std::filesystem::create_directories(Scratch.Path / ".hidden");
	Touch(Scratch.Path / "zz.MCR");
	Touch(Scratch.Path / "notes.txt");
	Touch(Scratch.Path / "Slot.mcs");
	Touch(Scratch.Path / "b" / "epsxe.mcd");

	FFileBrowser Browser(Scratch.Path / "no such folder" / "deeper");
	CHECK(Browser.Folder() == Scratch.Path);
	CHECK(Browser.FolderName() == "WeltallFileBrowser");
	CHECK(Names(Browser) == std::vector<std::string>{"..", "A", "b", "Slot.mcs", "zz.MCR"});

	// Into b, a file picked there, and back up.
	CHECK_FALSE(Browser.Open(2));
	CHECK(Browser.Folder() == Scratch.Path / "b");
	CHECK(Names(Browser) == std::vector<std::string>{"..", "epsxe.mcd"});
	const std::optional<std::filesystem::path> Picked = Browser.Open(1);
	REQUIRE(Picked);
	CHECK(*Picked == Scratch.Path / "b" / "epsxe.mcd");
	CHECK_FALSE(Browser.Open(0));
	CHECK(Browser.Folder() == Scratch.Path);
	CHECK(Browser.Up());
	CHECK(Browser.Folder() == Scratch.Path.parent_path());

	CHECK(FFileBrowser::DisplayName(std::filesystem::path(u8"Caérd{1}")) == "Ca?rd?1?");
	CHECK(FFileBrowser::IsOffered("card.Gme"));
	CHECK_FALSE(FFileBrowser::IsOffered("card.zip"));
}

TEST_CASE("A card written out is read back by browsing to it, its saves copied into a card at their numbers", "[Menu][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	const auto Resources = FMenuResources::Load(Disc->Image(), *Index, *Executable);
	REQUIRE(Resources);
	auto NewGame = LoadNewGameState(Disc->Image(), *Index, Resident.NameTables);
	REQUIRE(NewGame);

	FScratchFolder Scratch("WeltallSavePages");
	FSaveStore Store(Scratch.Path / "Saves");
	const FOriginalSave Third = WriteOriginalSave(*NewGame, 2, 0, Resident.NameTables, Resident.Font.LeadByte());
	const FOriginalSave Last = WriteOriginalSave(*NewGame, 14, 0, Resident.NameTables, Resident.Font.LeadByte());
	constexpr int32 SecondCard = FSaveStore::SlotsPerStore;
	REQUIRE_FALSE(Store.Write(SecondCard + 2, Third));
	REQUIRE_FALSE(Store.Write(SecondCard + 14, Last));
	std::filesystem::create_directories(Scratch.Path / "Saves" / "Cards");
	Touch(Scratch.Path / "Saves" / "Cards" / "broken.mcr", "not a card");

	FMenuSavePages Pages(Store, *Resources);
	CHECK(Pages.Title() == OptionsText::Titles::Saves);
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	// Export: Right to Card 2, Confirm.
	REQUIRE(Pages.Step(1, 1));
	CHECK(Pages.Rows()[1].Value == "Card 2");
	REQUIRE(Pages.Activate(1));
	CHECK(Pages.Rows()[1].Help == OptionsText::Fill(Text::Exported, {"Card 2"}));
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	CHECK(std::filesystem::file_size(Scratch.Path / "Saves" / "Cards" / "Card 2.mcr") == 0x20000);
	Pages.CursorMoved();
	CHECK(Pages.Rows()[1].Help == OptionsText::Fill(Text::Export.Help));

	// Import: browsing opens where the last import was picked from.
	Store.KeepImportFolder(Scratch.Path / "Saves" / "Cards");
	REQUIRE(Pages.Activate(0));
	REQUIRE(Pages.Page() == ESavesPage::Browse);
	CHECK(Pages.Title() == "Cards");
	std::vector<FSettingsRow> Rows = Pages.Rows();
	CHECK(AllFit(Rows, Resident.Font));
	const int32 Broken = RowNamed(Rows, "broken.mcr");
	REQUIRE(Broken >= 0);
	REQUIRE(Pages.Activate(Broken));
	CHECK(Pages.Page() == ESavesPage::Browse);
	CHECK(Pages.Rows()[static_cast<std::size_t>(Broken)].Help == OptionsText::Fill(Text::NotACard));
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	REQUIRE(Pages.Activate(RowNamed(Rows, "Card 2.mcr")));
	REQUIRE(Pages.Page() == ESavesPage::Import);

	// Into the first card with room for both, at their own numbers.
	Rows = Pages.Rows();
	CHECK(Rows[0].Value == "Card 1");
	CHECK(Rows[1].Label == OptionsText::Fill(Text::Copy.Label, {OptionsText::Fill(Text::ManySaves, {"2"})}));
	CHECK(Rows[2].Label == "No.03");
	CHECK(Rows[3].Label == "No.15");
	CHECK(AllFit(Rows, Resident.Font));
	REQUIRE(Pages.Activate(1));
	CHECK(Pages.Rows()[1].Help == OptionsText::Fill(Text::Copied, {OptionsText::Fill(Text::ManySaves, {"2"}), "Card 1"}));
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	for (const auto& [Slot, Save] : {std::pair{2, &Third}, std::pair{14, &Last}})
	{
		const auto Copied = Store.ReadBytes(Slot);
		REQUIRE(Copied);
		REQUIRE(*Copied);
		CHECK(std::equal((*Copied)->begin(), (*Copied)->end(), Save->Data().begin(), Save->Data().end()));
	}

	// Into Card 2, where both numbers hold saves: a second Confirm replaces them.
	REQUIRE(Pages.Step(0, 1));
	CHECK(Pages.Rows()[1].Value == OptionsText::Fill(Text::Overwrites, {"2"}));
	CHECK(Pages.Rows()[1].Help[0] == OptionsText::Fill(Text::OverwriteHelp[0], {"Card 2"}));
	CHECK(Pages.Rows()[1].Help[1] == "No.03 No.15");
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	REQUIRE_FALSE(Store.Remove(SecondCard + 2));
	REQUIRE(Pages.Activate(1));
	CHECK(Pages.Rows()[1].Value == OptionsText::Fill(Text::Overwrites, {"1"}));
	CHECK(Pages.Rows()[1].Help[0] == Text::OverwriteAsked[0]);
	CHECK(Pages.Rows()[1].Help[1] == "No.15");
	CHECK(Pages.Rows()[1].Help[2] == OptionsText::Fill(Text::OverwriteAsked[2], {"Card 2"}));
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	const auto Untouched = Store.ReadBytes(SecondCard + 2);
	REQUIRE(Untouched);
	CHECK_FALSE(*Untouched);
	REQUIRE(Pages.Activate(1));
	const auto Replaced = Store.ReadBytes(SecondCard + 2);
	REQUIRE(Replaced);
	CHECK(*Replaced);

	// Back to the folder, then to the saves page; no further.
	REQUIRE(Pages.Back());
	CHECK(Pages.Page() == ESavesPage::Browse);
	REQUIRE(Pages.Back());
	CHECK(Pages.Page() == ESavesPage::Saves);
	CHECK_FALSE(Pages.Back());
}

TEST_CASE("Everything the saves pages tell is in the game's font and fits the help", "[Menu][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	std::vector<FSettingsRow> Told;
	for (const std::array<std::string_view, 3>& Lines : {Text::Unreadable, Text::NotACard, Text::NoSaves, Text::CopyFailed, Text::DamagedCard, Text::WriteFailed,
		Text::DocumentPick, Text::DocumentDestination, Text::DocumentUnavailable, Text::DocumentCancelled, Text::DocumentTooLarge, Text::DocumentExported})
	{
		Told.push_back({"", "", OptionsText::Fill(Lines)});
	}
	Told.push_back({"", "", OptionsText::Fill(Text::Copied, {OptionsText::Fill(Text::ManySaves, {"15"}), "Card 4"})});
	Told.push_back({"", "", OptionsText::Fill(Text::Exported, {"Card 4"})});
	Told.push_back({"", "", OptionsText::Fill(Text::SaveHelp, {"No.15", "99:59:59"})});
	Told.push_back({OptionsText::Fill(Text::Copy.Label, {OptionsText::Fill(Text::ManySaves, {"15"})}), OptionsText::Fill(Text::Overwrites, {"15"}),
		OptionsText::Fill(Text::OverwriteHelp, {"Card 4"})});
	Told.push_back({std::string(Text::NoFolders), std::string(Text::Drive), OptionsText::Fill(Text::OverwriteAsked, {"Card 4"})});
	CHECK(AllFit(Told, Resident.Font));
}

TEST_CASE("System save documents preserve saves until import confirmation and report asynchronous results", "[Menu][Content][SaveDocuments]")
{
	struct FDocuments final : ISaveDocuments
	{
		FSaveDocumentResult Result;
		std::vector<uint8> Exported;
		std::string Name;
		int Imports = 0;
		int Cancels = 0;
		bool bAvailable = true;
		bool Import() override { ++Imports; Result = {ESaveDocumentStatus::Pending, {}}; return bAvailable; }
		bool Export(std::span<const uint8> Bytes, const std::string& Suggested) override
		{
			Exported.assign(Bytes.begin(), Bytes.end());
			Name = Suggested;
			Result = {ESaveDocumentStatus::Pending, {}};
			return bAvailable;
		}
		FSaveDocumentResult Poll() override
		{
			if (Result.Status == ESaveDocumentStatus::Pending) { return Result; }
			FSaveDocumentResult Taken = std::move(Result);
			Result = {};
			return Taken;
		}
		void Cancel() override { ++Cancels; Result = {}; }
	};
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FMesResident Resident = Testing::LoadMesResident(*Disc);
	const auto Executable = ReadBootExecutable(*Disc);
	REQUIRE(Executable);
	const auto Index = FGameFileIndex::Read(*Executable);
	REQUIRE(Index);
	const auto Resources = FMenuResources::Load(Disc->Image(), *Index, *Executable);
	REQUIRE(Resources);
	auto NewGame = LoadNewGameState(Disc->Image(), *Index, Resident.NameTables);
	REQUIRE(NewGame);
	FScratchFolder Scratch("WeltallSaveDocuments");
	FSaveStore Store(Scratch.Path / "Saves");
	FSaveStore Donor(Scratch.Path / "Donor");
	const FOriginalSave Original = WriteOriginalSave(*NewGame, 0, 0, Resident.NameTables, Resident.Font.LeadByte());
	NewGame->SetPlayTime(120);
	const FOriginalSave Imported = WriteOriginalSave(*NewGame, 0, 0, Resident.NameTables, Resident.Font.LeadByte());
	const auto InitialWrite = Store.Write(0, Original);
	INFO((InitialWrite ? InitialWrite->Message : "Initial save written"));
	REQUIRE_FALSE(InitialWrite);
	REQUIRE_FALSE(Donor.Write(0, Imported));
	const auto Card = ExportStoreCard(Donor, 0, *Resources);
	REQUIRE(Card);
	const auto Documents = std::make_shared<FDocuments>();
	Store.SetDocuments(Documents);
	FMenuSavePages Pages(Store, *Resources);
	const auto Before = Store.ReadBytes(0);
	REQUIRE(Before);
	const auto Unchanged = [&] { CHECK(Store.ReadBytes(0).Value() == Before.Value()); };

	REQUIRE(Pages.Activate(0));
	CHECK(Pages.Page() == ESavesPage::Saves);
	CHECK_FALSE(Pages.FollowDocuments());
	CHECK_FALSE(Pages.Activate(0));
	CHECK(Documents->Imports == 1);
	REQUIRE(Pages.Back());
	CHECK(Documents->Cancels == 1);
	Unchanged();
	for (const auto Status : {ESaveDocumentStatus::Cancelled, ESaveDocumentStatus::Failed, ESaveDocumentStatus::TooLarge})
	{
		REQUIRE(Pages.Activate(0));
		Documents->Result = {Status, {}};
		REQUIRE(Pages.FollowDocuments());
		CHECK_FALSE(Pages.FollowDocuments());
		CHECK(AllFit(Pages.Rows(), Resident.Font));
		Unchanged();
	}
	REQUIRE(Pages.Activate(0));
	Documents->Result = {ESaveDocumentStatus::Imported, {1, 2, 3}};
	REQUIRE(Pages.FollowDocuments());
	CHECK(Pages.Rows()[0].Help == OptionsText::Fill(Text::NotACard));
	Unchanged();
	REQUIRE(Pages.Activate(0));
	Documents->Result = {ESaveDocumentStatus::Imported, *Card};
	REQUIRE(Pages.FollowDocuments());
	REQUIRE(Pages.Page() == ESavesPage::Import);
	CHECK(Pages.Rows()[0].Value == "Card 2");
	REQUIRE(Pages.Step(0, -1));
	REQUIRE(Pages.Activate(1));
	Unchanged();
	REQUIRE(Pages.Activate(1));
	const std::vector<uint8> Expected(Imported.Data().begin(), Imported.Data().end());
	CHECK(Store.ReadBytes(0).Value() == std::optional<std::vector<uint8>>(Expected));
	REQUIRE(Pages.Back());
	CHECK(Pages.Page() == ESavesPage::Saves);
	REQUIRE(Pages.Activate(1));
	CHECK(Documents->Name == "Card 1.mcr");
	CHECK(Documents->Exported == ExportStoreCard(Store, 0, *Resources).Value());
	CHECK_FALSE(std::filesystem::exists(Scratch.Path / "Saves" / "Cards"));
	Documents->Result = {ESaveDocumentStatus::Exported, {}};
	REQUIRE(Pages.FollowDocuments());
	CHECK(Pages.Rows()[1].Help == OptionsText::Fill(Text::DocumentExported));
	CHECK(AllFit(Pages.Rows(), Resident.Font));
	{
		FMenuSavePages Abandoned(Store, *Resources);
		REQUIRE(Abandoned.Activate(0));
	}
	CHECK(Documents->Cancels == 2);
}
