#include "Menu/MenuSavePages.h"

#include "Game/SaveStore.h"
#include "Game/SaveDocuments.h"
#include "Menu/MenuSaveCards.h"
#include "Menu/OptionsText.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Weltall
{
	namespace
	{
		namespace Text = OptionsText::Saves;

		namespace SavesRow
		{
			enum : int32 { Import, Export };
		}

		namespace ImportRow
		{
			enum : int32 { Into, Copy, FirstSave };
		}

		/** A name shown in a row before its value, and a help line: in the font's 8-pixel letters. */
		constexpr std::size_t RowLetters = 25;
		constexpr std::size_t LineLetters = 30;
		constexpr std::size_t TitleLetters = 22;
		constexpr uint32 VblanksPerSecond = 60;

		std::string Shortened(const std::string& Name, std::size_t Letters)
		{
			return Name.size() <= Letters ? Name : Name.substr(0, Letters - 2) + "..";
		}

		/** A long name broken into the help's three lines, its end cut short where it does not fit. */
		std::array<std::string, 3> Wrapped(const std::string& Name)
		{
			std::array<std::string, 3> Lines;
			for (std::size_t Line = 0; Line < Lines.size() && Line * LineLetters < Name.size(); ++Line)
			{
				Lines[Line] = Name.substr(Line * LineLetters, LineLetters);
			}
			if (Name.size() > Lines.size() * LineLetters)
			{
				Lines.back() = Shortened(Name.substr((Lines.size() - 1) * LineLetters), LineLetters);
			}
			return Lines;
		}

		std::string CardName(int32 Store)
		{
			return OptionsText::Fill(Text::Card, {std::to_string(Store + 1)});
		}

		/** A row as the screen shows it: the words' label and help, and its value. */
		FSettingsRow RowOfWords(const OptionsText::FRow& Words, std::string Value = {})
		{
			return {std::string(Words.Label), std::move(Value), OptionsText::Fill(Words.Help)};
		}

		std::string PlayTime(uint32 Vblanks)
		{
			const uint32 Seconds = Vblanks / VblanksPerSecond;
			char Text[40];
			std::snprintf(Text, sizeof Text, "%u:%02u:%02u", Seconds / 3600, Seconds / 60 % 60, Seconds % 60);
			return Text;
		}

		std::string SaveNumber(const FOriginalSave& Save)
		{
			char Text[16];
			std::snprintf(Text, sizeof Text, "No.%02d", Save.CardSlot() + 1);
			return Text;
		}

		std::string SaveCount(int32 Count)
		{
			return Count == 1 ? std::string(Text::OneSave) : OptionsText::Fill(Text::ManySaves, {std::to_string(Count)});
		}

		/** The user's home: where browsing starts the first time. */
		std::filesystem::path Home()
		{
#ifdef _WIN32
			wchar_t* Value = nullptr;
			std::size_t Length = 0;
			if (_wdupenv_s(&Value, &Length, L"USERPROFILE") == 0 && Value != nullptr)
			{
				const std::filesystem::path Folder(Value);
				std::free(Value);
				return Folder;
			}
#else
			if (const char* Value = std::getenv("HOME"))
			{
				return Value;
			}
#endif
			std::error_code Error;
			return std::filesystem::current_path(Error);
		}

		std::optional<std::vector<uint8>> ReadFile(const std::filesystem::path& Path)
		{
			std::ifstream File(Path, std::ios::binary);
			if (!File)
			{
				return std::nullopt;
			}
			return std::vector<uint8>((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		}
	}

	FMenuSavePages::FMenuSavePages(FSaveStore& InSaves, const FMenuResources& InResources) : Saves(InSaves), Resources(InResources)
	{
	}

	FMenuSavePages::~FMenuSavePages()
	{
		if (bDocumentPending && Saves.SaveDocuments()) { Saves.SaveDocuments()->Cancel(); }
	}

	bool FMenuSavePages::FollowDocuments()
	{
		if (!bDocumentPending || !Saves.SaveDocuments()) { return false; }
		FSaveDocumentResult Result = Saves.SaveDocuments()->Poll();
		if (Result.Status == ESaveDocumentStatus::Pending) { return false; }
		bDocumentPending = false;
		const int32 Row = bDocumentExport ? SavesRow::Export : SavesRow::Import;
		if (Result.Status == ESaveDocumentStatus::Imported && !bDocumentExport) { PickBytes(Result.Bytes, Row); }
		else if (Result.Status == ESaveDocumentStatus::Exported && bDocumentExport) { Tell(Row, OptionsText::Fill(Text::DocumentExported)); }
		else if (Result.Status == ESaveDocumentStatus::Cancelled) { Tell(Row, OptionsText::Fill(Text::DocumentCancelled)); }
		else if (Result.Status == ESaveDocumentStatus::TooLarge) { Tell(Row, OptionsText::Fill(Text::DocumentTooLarge)); }
		else { Tell(Row, OptionsText::Fill(bDocumentExport ? Text::WriteFailed : Text::Unreadable)); }
		return true;
	}

	std::string FMenuSavePages::Title() const
	{
		switch (Shown)
		{
		case ESavesPage::Saves:
			return std::string(OptionsText::Titles::Saves);
		case ESavesPage::Browse:
			return Shortened(Browser->FolderName(), TitleLetters);
		case ESavesPage::Import:
			return std::string(OptionsText::Titles::Import);
		}
		return {};
	}

	std::vector<FSettingsRow> FMenuSavePages::Rows() const
	{
		std::vector<FSettingsRow> Rows;
		switch (Shown)
		{
		case ESavesPage::Saves:
			Rows.push_back(RowOfWords(Text::Import));
			Rows.push_back(RowOfWords(Saves.SaveDocuments() ? Text::DocumentExport : Text::Export, CardName(ExportStore)));
			break;
		case ESavesPage::Browse:
			for (const FBrowserEntry& Entry : Browser->Entries())
			{
				switch (Entry.Kind)
				{
				case FBrowserEntry::EKind::Up:
					Rows.push_back(RowOfWords(Text::Up));
					break;
				case FBrowserEntry::EKind::Folder:
					Rows.push_back({Shortened(Entry.Name, RowLetters), std::string(Browser->Folder().empty() ? Text::Drive : Text::Folder), Wrapped(Entry.Name)});
					break;
				case FBrowserEntry::EKind::File:
					Rows.push_back({Shortened(Entry.Name, RowLetters), "", Wrapped(Entry.Name)});
					break;
				}
			}
			if (Rows.empty())
			{
				Rows.push_back({std::string(Text::NoFolders), "", {"", "", ""}});
			}
			break;
		case ESavesPage::Import:
		{
			Rows.push_back(RowOfWords(Text::Into, CardName(IntoStore)));
			const int32 Replacing = Replaced(IntoStore);
			FSettingsRow Copy = RowOfWords(Text::Copy);
			Copy.Label = OptionsText::Fill(Text::Copy.Label, {SaveCount(static_cast<int32>(Picked.size()))});
			if (Replacing > 0)
			{
				Copy.Value = OptionsText::Fill(Text::Overwrites, {std::to_string(Replacing)});
				Copy.Help = OptionsText::Fill(Text::OverwriteHelp, {CardName(IntoStore)});
				Copy.Help[1] = ReplacedNumbers(IntoStore);
			}
			if (bReplaceAsked)
			{
				Copy.Help = OptionsText::Fill(Text::OverwriteAsked, {CardName(IntoStore)});
				Copy.Help[1] = ReplacedNumbers(IntoStore);
			}
			Rows.push_back(std::move(Copy));
			for (const FOriginalSave& Save : Picked)
			{
				Rows.push_back({SaveNumber(Save), PlayTime(Save.PlayTime()), OptionsText::Fill(Text::SaveHelp, {SaveNumber(Save), PlayTime(Save.PlayTime())})});
			}
			break;
		}
		}
		if (ToldRow >= 0 && ToldRow < static_cast<int32>(Rows.size()))
		{
			Rows[static_cast<std::size_t>(ToldRow)].Help = Told;
		}
		return Rows;
	}

	bool FMenuSavePages::Step(int32 Row, int32 Direction)
	{
		if (bDocumentPending) { return false; }
		int32* Store = nullptr;
		if (Shown == ESavesPage::Saves && Row == SavesRow::Export)
		{
			Store = &ExportStore;
		}
		else if (Shown == ESavesPage::Import && Row == ImportRow::Into)
		{
			Store = &IntoStore;
		}
		if (Store == nullptr)
		{
			return false;
		}
		*Store = (*Store + Direction + FSaveStore::StoreCount) % FSaveStore::StoreCount;
		bReplaceAsked = false;
		ToldRow = -1;
		return true;
	}

	bool FMenuSavePages::Activate(int32 Row)
	{
		if (bDocumentPending) { return false; }
		switch (Shown)
		{
		case ESavesPage::Saves:
			if (Row == SavesRow::Import)
			{
				Browse();
				return true;
			}
			Export();
			return true;
		case ESavesPage::Browse:
			if (Row < 0 || Row >= static_cast<int32>(Browser->Entries().size()))
			{
				return false;
			}
			if (const std::optional<std::filesystem::path> File = Browser->Open(Row))
			{
				Pick(*File);
			}
			else
			{
				++Views;
			}
			return true;
		case ESavesPage::Import:
			if (Row != ImportRow::Copy)
			{
				return false;
			}
			if (Replaced(IntoStore) > 0 && !bReplaceAsked)
			{
				bReplaceAsked = true;
				return true;
			}
			Import();
			return true;
		}
		return false;
	}

	bool FMenuSavePages::Back()
	{
		if (bDocumentPending && Saves.SaveDocuments())
		{
			Saves.SaveDocuments()->Cancel();
			bDocumentPending = false;
			Tell(bDocumentExport ? SavesRow::Export : SavesRow::Import, OptionsText::Fill(Text::DocumentCancelled));
			return true;
		}
		ToldRow = -1;
		bReplaceAsked = false;
		switch (Shown)
		{
		case ESavesPage::Saves:
			return false;
		case ESavesPage::Browse:
			Shown = ESavesPage::Saves;
			++Views;
			return true;
		case ESavesPage::Import:
			Shown = Saves.SaveDocuments() ? ESavesPage::Saves : ESavesPage::Browse;
			++Views;
			return true;
		}
		return false;
	}

	void FMenuSavePages::CursorMoved()
	{
		ToldRow = -1;
		bReplaceAsked = false;
	}

	void FMenuSavePages::Browse()
	{
		if (ISaveDocuments* Documents = Saves.SaveDocuments())
		{
			bDocumentExport = false;
			bDocumentPending = Documents->Import();
			Tell(SavesRow::Import, OptionsText::Fill(bDocumentPending ? Text::DocumentPick : Text::DocumentUnavailable));
			return;
		}
		Browser.emplace(Saves.ImportFolder().value_or(Home()));
		Shown = ESavesPage::Browse;
		++Views;
		ToldRow = -1;
	}

	void FMenuSavePages::Pick(const std::filesystem::path& File)
	{
		const int32 Row = [&]
		{
			const std::vector<FBrowserEntry>& Entries = Browser->Entries();
			for (std::size_t Entry = 0; Entry < Entries.size(); ++Entry)
			{
				if (Entries[Entry].Path == File)
				{
					return static_cast<int32>(Entry);
				}
			}
			return -1;
		}();
		const std::optional<std::vector<uint8>> Bytes = ReadFile(File);
		if (!Bytes)
		{
			Tell(Row, OptionsText::Fill(Text::Unreadable));
			return;
		}
		PickBytes(*Bytes, Row);
		if (Shown == ESavesPage::Import) { Saves.KeepImportFolder(Browser->Folder()); }
	}

	void FMenuSavePages::PickBytes(std::span<const uint8> Bytes, int32 Row)
	{
		TResult<std::vector<FOriginalSave>> Read = ReadCardImageSaves(Bytes);
		if (!Read)
		{
			Tell(Row, OptionsText::Fill(Text::NotACard));
			return;
		}
		if (Read->empty())
		{
			Tell(Row, OptionsText::Fill(Text::NoSaves));
			return;
		}
		Picked = std::move(*Read);
		// The first card with room for every save at its number, else the first.
		IntoStore = 0;
		for (int32 Store = 0; Store < FSaveStore::StoreCount; ++Store)
		{
			if (Replaced(Store) == 0)
			{
				IntoStore = Store;
				break;
			}
		}
		bReplaceAsked = false;
		ToldRow = -1;
		Shown = ESavesPage::Import;
		++Views;
	}

	void FMenuSavePages::Import()
	{
		bReplaceAsked = false;
		for (const FOriginalSave& Save : Picked)
		{
			if (Saves.Copy(IntoStore * FSaveStore::SlotsPerStore + Save.CardSlot(), Save.Data()))
			{
				Tell(ImportRow::Copy, OptionsText::Fill(Text::CopyFailed));
				return;
			}
		}
		Tell(ImportRow::Copy, OptionsText::Fill(Text::Copied, {SaveCount(static_cast<int32>(Picked.size())), CardName(IntoStore)}));
	}

	void FMenuSavePages::Export()
	{
		TResult<std::vector<uint8>> Image = ExportStoreCard(Saves, ExportStore, Resources);
		if (!Image)
		{
			Tell(SavesRow::Export, OptionsText::Fill(Text::DamagedCard));
			return;
		}
		if (ISaveDocuments* Documents = Saves.SaveDocuments())
		{
			bDocumentExport = true;
			bDocumentPending = Documents->Export(*Image, CardName(ExportStore) + ".mcr");
			Tell(SavesRow::Export, OptionsText::Fill(bDocumentPending ? Text::DocumentDestination : Text::DocumentUnavailable));
			return;
		}
		if (!Saves.WriteCardImage(ExportStore, *Image))
		{
			Tell(SavesRow::Export, OptionsText::Fill(Text::WriteFailed));
			return;
		}
		Tell(SavesRow::Export, OptionsText::Fill(Text::Exported, {CardName(ExportStore)}));
	}

	bool FMenuSavePages::HoldsSave(int32 Store, const FOriginalSave& Save) const
	{
		// A slot whose file cannot even be read counts as holding a save: it is never replaced unasked.
		const TResult<std::optional<std::vector<uint8>>> There = Saves.ReadBytes(Store * FSaveStore::SlotsPerStore + Save.CardSlot());
		return !There || *There;
	}

	int32 FMenuSavePages::Replaced(int32 Store) const
	{
		int32 Count = 0;
		for (const FOriginalSave& Save : Picked)
		{
			Count += HoldsSave(Store, Save) ? 1 : 0;
		}
		return Count;
	}

	std::string FMenuSavePages::ReplacedNumbers(int32 Store) const
	{
		// Five fit a help line; past five, four and how many more.
		constexpr std::size_t Listed = 5;
		std::vector<std::string> Numbers;
		for (const FOriginalSave& Save : Picked)
		{
			if (HoldsSave(Store, Save))
			{
				Numbers.push_back(SaveNumber(Save));
			}
		}
		std::string Line;
		const std::size_t Named = Numbers.size() <= Listed ? Numbers.size() : Listed - 1;
		for (std::size_t Number = 0; Number < Named; ++Number)
		{
			Line += (Line.empty() ? "" : " ") + Numbers[Number];
		}
		if (Named < Numbers.size())
		{
			Line += " +" + std::to_string(Numbers.size() - Named);
		}
		return Line;
	}

	void FMenuSavePages::Tell(int32 Row, std::array<std::string, 3> Lines)
	{
		ToldRow = Row;
		Told = std::move(Lines);
	}
}
