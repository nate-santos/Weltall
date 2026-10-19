#include "Game/GameTextExport.h"

#include "Game/GameText.h"
#include "Media/AssetFiles.h"
#include "Media/GameFileIndex.h"
#include "Media/Iso9660.h"
#include "Media/PsxExecutable.h"
#include "Text/MesResident.h"
#include "Text/MesText.h"
#include "Text/MessageOverrides.h"

#include <set>
#include <span>
#include <unordered_map>
#include <vector>

namespace Weltall
{
	namespace
	{
		/** Files a step writes: a step stays well under a frame's worth of a worker's time for the field maps. */
		constexpr std::size_t FilesPerStep = 16;
		constexpr std::string_view OriginalsFolder = FMessageOverrides::OriginalsFolder;

		TResult<bool> Publish(const std::filesystem::path& Folder, const std::filesystem::path& Relative, std::string_view Text)
		{
			return PublishAssetFile(Folder, std::filesystem::path(std::string(OriginalsFolder)) / Relative,
				std::span<const uint8>(reinterpret_cast<const uint8*>(Text.data()), Text.size()));
		}
	}

	struct FGameTextExport::FState
	{
		FState(FIso9660 InDisc, FGameFileIndex InIndex, FPsxExecutable InExecutable, FMesFont InFont, std::filesystem::path InTextFolder, std::filesystem::path InFontFolder)
			: Disc(std::move(InDisc)), Index(std::move(InIndex)), Executable(std::move(InExecutable)), Font(std::move(InFont)), TextFolder(std::move(InTextFolder)),
			  FontFolder(std::move(InFontFolder)), Picture(MesFontSheet::Picture(Font))
		{
		}

		FIso9660 Disc;
		FGameFileIndex Index;
		FPsxExecutable Executable;
		FMesFont Font;
		std::filesystem::path TextFolder;
		std::filesystem::path FontFolder;
		FRgbaPicture Picture;
		std::vector<FGameTextFile> Files = GameTextFiles();
		std::size_t Next = 0;
		/** How many messages type each glyph, by its code's lead << 8 | code. */
		std::unordered_map<uint16, int32> Uses;
	};

	FGameTextExport::FGameTextExport(std::unique_ptr<FState> InState) : State(std::move(InState)) {}

	FGameTextExport::~FGameTextExport() = default;

	TResult<std::unique_ptr<FGameTextExport>> FGameTextExport::Open(const FIso9660& Disc, const FGameFileIndex& Index, std::filesystem::path TextFolder,
		std::filesystem::path FontFolder)
	{
		FIso9660 Alone = Disc;
		Alone.Image().UseDiscAlone();
		TResult<FPsxExecutable> Executable = ReadBootExecutable(Alone);
		if (!Executable)
		{
			return Executable.Error();
		}
		TResult<FMesResident> Resident = LoadMesResident(Alone.Image(), Index, *Executable);
		if (!Resident)
		{
			return Resident.Error();
		}
		auto State = std::make_unique<FState>(std::move(Alone), Index, std::move(*Executable), std::move(Resident->Font), std::move(TextFolder), std::move(FontFolder));
		return std::unique_ptr<FGameTextExport>(new FGameTextExport(std::move(State)));
	}

	TResult<bool> FGameTextExport::Step()
	{
		for (std::size_t Written = 0; Written < FilesPerStep && State->Next < State->Files.size(); ++Written, ++State->Next)
		{
			const FGameTextFile File = State->Files[State->Next];
			const TResult<std::vector<FGameTextTable>> Tables = ReadGameText(State->Disc.Image(), State->Index, State->Executable, File);
			if (!Tables)
			{
				return MakeError(GameTextFilePath(File) + ": " + Tables.Error().Message);
			}
			if (Tables->empty())
			{
				continue;
			}
			for (const FGameTextTable& Table : *Tables)
			{
				for (const FGameTextMessage& Message : Table.Messages)
				{
					std::set<uint16> Typed;
					for (const FMesCode Glyph : MesGlyphsOfCodes(Message.Codes))
					{
						Typed.insert(static_cast<uint16>(Glyph.Lead << 8 | Glyph.Code));
					}
					for (const uint16 Glyph : Typed)
					{
						++State->Uses[Glyph];
					}
				}
			}
			if (const TResult<bool> Published = Publish(State->TextFolder, GameTextFilePath(File), WriteGameText(File, *Tables)); !Published)
			{
				return Published.Error();
			}
		}
		if (State->Next < State->Files.size())
		{
			return false;
		}
		const std::string Table = MesFontSheet::Table(State->Font, MesFontSheet::Glyphs(FMesCharacters::Font()), State->Uses);
		if (const TResult<bool> Published = Publish(State->FontFolder, "font.tsv", Table); !Published)
		{
			return Published.Error();
		}
		return true;
	}

	int32 FGameTextExport::CompletedFiles() const
	{
		return static_cast<int32>(State->Next);
	}

	int32 FGameTextExport::TotalFiles() const
	{
		return static_cast<int32>(State->Files.size());
	}

	const FRgbaPicture& FGameTextExport::FontPicture() const
	{
		return State->Picture;
	}
}
