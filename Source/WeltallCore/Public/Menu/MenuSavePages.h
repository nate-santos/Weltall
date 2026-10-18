#pragma once

#include "Foundation/Types.h"
#include "Game/OriginalSave.h"
#include "Menu/MenuFileBrowser.h"
#include "Menu/MenuSettingsPages.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Weltall
{
	class FMenuResources;
	class FSaveStore;

	/** The options screen's pages for the player's saves: the page itself, a folder's files, a file's saves to import. */
	enum class ESavesPage : uint8
	{
		Saves,
		Browse,
		Import,
	};

	/**
	 * The saves' way in and out: a memory card image or a save's file picked by browsing the computer's
	 * folders, its saves copied into one of the player's eight cards at their own numbers (a second Confirm
	 * before any replaces a save there); and a card written as a memory card image for an emulator or a console.
	 * Each row as the options screen shows it; what an action did is told in the help of its row until the
	 * cursor moves.
	 */
	class FMenuSavePages
	{
	public:
		/** Browsing starts in the folder last imported from, else in the user's home. Both must outlive the pages. */
		WELTALLCORE_API FMenuSavePages(FSaveStore& InSaves, const FMenuResources& InResources);
		WELTALLCORE_API ~FMenuSavePages();
		/** Consumes a completed system-picker operation; true when the displayed rows changed. */
		WELTALLCORE_API bool FollowDocuments();

		ESavesPage Page() const { return Shown; }
		/** Counts the pages and folders shown: a new one shows its rows from the top. */
		uint32 View() const { return Views; }
		WELTALLCORE_API std::string Title() const;
		WELTALLCORE_API std::vector<FSettingsRow> Rows() const;

		/** Left (-1) or Right (+1) on a row: false where nothing changed. */
		WELTALLCORE_API bool Step(int32 Row, int32 Direction);
		/** Confirm on a row: false where it does nothing. The page shown may change. */
		WELTALLCORE_API bool Activate(int32 Row);
		/** Cancel: the page before; false on the first. */
		WELTALLCORE_API bool Back();
		/** The cursor left its row: what the last action told is gone, and a replacement asked about is not done. */
		WELTALLCORE_API void CursorMoved();

	private:
		void Browse();
		void Pick(const std::filesystem::path& File);
		void PickBytes(std::span<const uint8> Bytes, int32 Row);
		void Import();
		void Export();
		/** How many of the file's saves would replace one in the card. */
		int32 Replaced(int32 Store) const;
		/** The numbers of the card's saves the file's would replace, as many as a help line holds. */
		std::string ReplacedNumbers(int32 Store) const;
		bool HoldsSave(int32 Store, const FOriginalSave& Save) const;
		void Tell(int32 Row, std::array<std::string, 3> Lines);

		FSaveStore& Saves;
		const FMenuResources& Resources;
		ESavesPage Shown = ESavesPage::Saves;
		uint32 Views = 0;
		int32 ExportStore = 0;
		std::optional<FFileBrowser> Browser;
		std::vector<FOriginalSave> Picked;
		int32 IntoStore = 0;
		bool bReplaceAsked = false;
		bool bDocumentPending = false;
		bool bDocumentExport = false;
		int32 ToldRow = -1;
		std::array<std::string, 3> Told;
	};
}
