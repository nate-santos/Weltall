#include "Media/DiscModFiles.h"

#include "Foundation/FileByteSource.h"
#include "Foundation/LittleEndian.h"
#include "Media/CdImage.h"
#include "Media/GameFileIndex.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/** The second disc's index has five movies fewer than the first's: every record after them is five lower. */
		constexpr int32 SecondDiscShift = 5;
		/** A field map's header words: its scripts' offset (section 5) and the next section's, which ends them. */
		constexpr std::size_t ScriptsAt = 0x51 * 4;
		constexpr std::size_t ScriptsEndAt = 0x52 * 4;
		constexpr uint64 LargestFile = 512ULL * 1024 * 1024;

		TResult<std::vector<uint8>> ReadWhole(const std::filesystem::path& Path)
		{
			const auto File = FFileByteSource::Open(Path);
			if (!File)
			{
				return File.Error();
			}
			if ((*File)->Size() == 0 || (*File)->Size() > LargestFile)
			{
				return MakeError("a mod's file has an invalid size: " + Path.string());
			}
			std::vector<uint8> Bytes(static_cast<std::size_t>((*File)->Size()));
			if (!(*File)->Read(0, Bytes))
			{
				return MakeError("cannot read a mod's file: " + Path.string());
			}
			return Bytes;
		}

		/** Scripts written over the map's own, from their offset to the next section's: cut, or ended with zeros, to fit. */
		std::optional<FError> SpliceScripts(std::vector<uint8>& Map, std::span<const uint8> Scripts, const std::filesystem::path& Path)
		{
			if (Map.size() < ScriptsEndAt + 4)
			{
				return MakeError("a mod's map is too short for its sections: " + Path.string());
			}
			const uint32 Start = LittleEndian::ReadU32(Map, ScriptsAt);
			const uint32 End = LittleEndian::ReadU32(Map, ScriptsEndAt);
			if (Start > End || End > Map.size())
			{
				return MakeError("a mod's map has no place for its scripts: " + Path.string());
			}
			const std::size_t Kept = std::min<std::size_t>(Scripts.size(), End - Start);
			std::copy_n(Scripts.begin(), Kept, Map.begin() + Start);
			std::fill(Map.begin() + Start + static_cast<std::ptrdiff_t>(Kept), Map.begin() + End, uint8{0});
			return std::nullopt;
		}
	}

	std::shared_ptr<const FDiscModFiles> FDiscModFiles::Make(const FGameFileIndex& Index, int32 Disc, std::span<const FModFile> Files,
		const FModProgramChanges& Changes)
	{
		auto Made = std::make_shared<FDiscModFiles>();
		Made->ProgramChanges = Changes;
		const std::vector<FIndexRecord>& Records = Index.Records();
		for (const FModFile& File : Files)
		{
			const int32 Record = Disc == 2 ? File.Record - SecondDiscShift : File.Record;
			if (Record < 0 || static_cast<std::size_t>(Record) >= Records.size() || !Records[static_cast<std::size_t>(Record)].IsFile())
			{
				continue;
			}
			Made->BySector[Records[static_cast<std::size_t>(Record)].Lba] = File;
		}
		return Made;
	}

	TResult<std::optional<FFileReplacement>> FDiscModFiles::Read(const FIndexRecord& Record) const
	{
		const auto Found = BySector.find(Record.Lba);
		if (!Record.IsFile() || Found == BySector.end())
		{
			return std::optional<FFileReplacement>{};
		}
		const FModFile& File = Found->second;
		TResult<std::vector<uint8>> Bytes = ReadWhole(File.Path);
		if (!Bytes)
		{
			return Bytes.Error();
		}
		if (!File.Section5.empty())
		{
			const TResult<std::vector<uint8>> Scripts = ReadWhole(File.Section5);
			if (!Scripts)
			{
				return Scripts.Error();
			}
			if (std::optional<FError> Error = SpliceScripts(*Bytes, *Scripts, File.Path))
			{
				return *Error;
			}
		}
		FFileReplacement Replacement{std::move(*Bytes), 0};
		Replacement.Size = Replacement.Bytes.size();
		// The game reads whole sectors, and a packed stream may run on into the last one's padding.
		Replacement.Bytes.resize((Replacement.Size + CdSector::Form1DataSize - 1) / CdSector::Form1DataSize * CdSector::Form1DataSize, 0);
		return std::optional{std::move(Replacement)};
	}
}
