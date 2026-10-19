#pragma once

#include "CoreMinimal.h"
#include "Text/MesFontSheet.h"
#include "Text/MessageOverrides.h"

#include <filesystem>
#include <memory>

/** The player's font sheet in Folder (Assets/font: font.tsv, font.png), none where neither is there; problems are logged. */
WELTALL_API std::shared_ptr<const Weltall::FMesFontSheet> FindFontSheet(const FString& Folder);

/** The player's messages below Folder (Assets/text), typed in the characters and measured with the widths of Sheet (none: the font's own); problems are logged. */
WELTALL_API std::shared_ptr<const Weltall::FMessageOverrides> FindMessageOverrides(const std::filesystem::path& Folder, const Weltall::FMesFontSheet* Sheet);

/** Saves the export's font sheet picture as Folder's original/font.png, unless a file has that name. */
WELTALL_API void SaveFontSheetPicture(const FString& Folder, const Weltall::FRgbaPicture& Picture);
