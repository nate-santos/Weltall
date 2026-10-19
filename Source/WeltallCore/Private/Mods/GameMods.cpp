#include "Mods/GameMods.h"

#include "Media/DiscIdentification.h"
#include "Media/GameFileIndex.h"
#include "Media/GameLibrary.h"
#include "Media/PsxExecutable.h"

namespace Weltall
{
	FFoundMods FindMods(const std::filesystem::path& Folder)
	{
		return {FPerfectWorksRelease::Find(Folder / "PerfectWorks")};
	}

	std::optional<FError> ApplyMods(const FGameLibrary& Library, const FFoundMods& Mods, const FPerfectWorksOptions& Options)
	{
		for (const EDiscRelease Release : Library.Releases())
		{
			if (Release == EDiscRelease::JapanDisc1 || Release == EDiscRelease::JapanDisc2)
			{
				continue;
			}
			const FIso9660* Disc = Library.Find(Release);
			const int32 Number = DiscNumberOfRelease(Release);
			if (!Mods.PerfectWorks || !Options.bEnabled)
			{
				Disc->Image().SetModFiles(nullptr);
				continue;
			}
			const auto Executable = ReadBootExecutable(*Disc);
			if (!Executable)
			{
				return Executable.Error();
			}
			const auto Index = FGameFileIndex::Read(*Executable);
			if (!Index)
			{
				return Index.Error();
			}
			const std::vector<FModFile> Files = Mods.PerfectWorks->Files(Options, Number);
			Disc->Image().SetModFiles(FDiscModFiles::Make(*Index, Number, Files, Mods.PerfectWorks->Changes(Options, Files)));
		}
		return std::nullopt;
	}
}
