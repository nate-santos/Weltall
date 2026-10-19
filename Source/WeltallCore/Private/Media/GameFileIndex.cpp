#include "Media/GameFileIndex.h"

#include "Foundation/LittleEndian.h"
#include "Foundation/Sha1.h"

#include <string>

namespace Weltall
{
	namespace
	{
		/** Where the executable keeps the two tables (both discs of the USA release). */
		constexpr uint32 IndexAddress = 0x80010004;
		constexpr uint32 IndexBytes = 0x7FFC;
		constexpr uint32 DirectoryAddress = 0x80018004;

		constexpr std::size_t RecordBytes = 7;
		constexpr uint32 EndOfIndex = 0xFFFFFF;
		constexpr uint16 NoDirectory = 0xFFFF;

		bool HasUndubCodeInUnusedSlots(const FPsxExecutable& Executable)
		{
			const std::string Hash = FSha1::ToHex(FSha1::Hash(Executable.Body()));
			return Hash == "79c94ee8f3a9586998ec9dded3301a061ba3d1d9" || Hash == "a4fe3ac2b18a28691d8107ad6e0d3272f617876a";
		}
	}

	TResult<FGameFileIndex> FGameFileIndex::Read(const FPsxExecutable& Executable)
	{
		const std::span<const uint8> Index = Executable.Read(IndexAddress, IndexBytes);
		const std::span<const uint8> Directories = Executable.Read(DirectoryAddress, DirectoryCount * 2);
		if (Index.empty() || Directories.empty())
		{
			return MakeError("the executable holds no file index");
		}
		FGameFileIndex Result;
		for (std::size_t Offset = 0; Offset + RecordBytes <= Index.size(); Offset += RecordBytes)
		{
			const uint32 Lba = Index[Offset] | Index[Offset + 1] << 8 | Index[Offset + 2] << 16;
			if (Lba == EndOfIndex)
			{
				break;
			}
			Result.AllRecords.push_back({Lba, static_cast<int32>(LittleEndian::ReadU32(Index, Offset + 3))});
		}
		for (int32 Directory = 0; Directory < DirectoryCount; ++Directory)
		{
			Result.DirectoryStarts[static_cast<std::size_t>(Directory)] = LittleEndian::ReadU16(Directories, static_cast<std::size_t>(Directory) * 2);
		}
		// These supported undubs reuse unused index slots for their program's code and state, and keep their movie
		// subtitles past the records.
		if (HasUndubCodeInUnusedSlots(Executable) && Result.AllRecords.size() > 381)
		{
			for (std::size_t Record = 280; Record <= 376; ++Record) Result.AllRecords[Record] = {};
			Result.AllRecords[381] = {};
			Result.Subtitles = FMovieSubtitleTable::Read(Executable);
		}
		return Result;
	}

	std::optional<FGameFileIndex::FFileName> FGameFileIndex::NameOf(std::size_t Record) const
	{
		std::optional<FFileName> Name;
		std::size_t ClosestStart = 0;
		for (int32 Directory = 0; Directory < DirectoryCount; ++Directory)
		{
			const uint16 Start = DirectoryStarts[static_cast<std::size_t>(Directory)];
			if (Start == 0 || Start == NoDirectory || static_cast<std::size_t>(Start - 1) > Record || (Name && static_cast<std::size_t>(Start - 1) <= ClosestStart))
			{
				continue;
			}
			ClosestStart = static_cast<std::size_t>(Start - 1);
			Name = FFileName{Directory, static_cast<int32>(Record - ClosestStart + 1)};
		}
		return Name;
	}

	const FIndexRecord* FGameFileIndex::Find(int32 Directory, int32 Number) const
	{
		if (Directory < 0 || Directory >= DirectoryCount || Number < 1)
		{
			return nullptr;
		}
		const uint16 Start = DirectoryStarts[static_cast<std::size_t>(Directory)];
		if (Start == 0 || Start == NoDirectory)
		{
			return nullptr;
		}
		// File 1 is the directory's first record itself, so file n is n - 1 records after it.
		const std::size_t Record = static_cast<std::size_t>(Start - 1) + static_cast<std::size_t>(Number - 1);
		return Record < AllRecords.size() ? &AllRecords[Record] : nullptr;
	}

	TResult<std::vector<uint8>> ReadFileSectors(const FCdImage& Image, const FIndexRecord& Record)
	{
		if (!Record.IsFile())
		{
			return MakeError("index record at sector " + std::to_string(Record.Lba) + " holds no file");
		}
		auto Replacement = Image.ReadReplacement(Record);
		if (!Replacement) return Replacement.Error();
		if (*Replacement) return std::move((*Replacement)->Bytes);
		const uint64 Sectors = (static_cast<uint64>(Record.Size) + CdSector::Form1DataSize - 1) / CdSector::Form1DataSize;
		return Image.ReadForm1(Record.Lba, Sectors * CdSector::Form1DataSize);
	}

	TResult<std::vector<uint8>> ReadFileBytes(const FCdImage& Image, const FIndexRecord& Record)
	{
		if (!Record.IsFile())
		{
			return MakeError("index record at sector " + std::to_string(Record.Lba) + " holds no file");
		}
		auto Replacement = Image.ReadReplacement(Record);
		if (!Replacement) return Replacement.Error();
		if (*Replacement)
		{
			std::vector<uint8> Bytes = std::move((*Replacement)->Bytes);
			Bytes.resize(static_cast<std::size_t>((*Replacement)->Size));
			return Bytes;
		}
		TResult<std::vector<uint8>> Sectors = Image.ReadForm1(Record.Lba, static_cast<uint64>(Record.Size));
		return Sectors;
	}
}
