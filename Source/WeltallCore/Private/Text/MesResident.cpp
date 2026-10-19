#include "Text/MesResident.h"

#include "Formats/Lzss.h"
#include "Foundation/LittleEndian.h"
#include "Media/CdImage.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Text/MesFontSheet.h"
#include "Text/MessageOverrides.h"
#include "Text/MessageTableIds.h"

#include <string>

namespace Weltall
{
	namespace
	{
		/** Where the kernel keeps them (both discs of the USA release). */
		constexpr int32 ResidentDirectory = 1;
		constexpr int32 FontFile = 6;
		constexpr int32 NameTablesFile = 7;
		constexpr uint32 BoxGlyphAddress = 0x800501D0;

		TResult<std::vector<uint8>> ReadPacked(const FCdImage& Image, const FGameFileIndex& Index, int32 Number)
		{
			const FIndexRecord* Record = Index.Find(ResidentDirectory, Number);
			if (Record == nullptr)
			{
				return MakeError("the disc lacks file " + std::to_string(Number) + " of directory 1");
			}
			const TResult<std::vector<uint8>> Sectors = ReadFileSectors(Image, *Record);
			if (!Sectors)
			{
				return Sectors.Error();
			}
			return UnpackLzss(*Sectors);
		}

		/** The name tables' file with the player's names in place: a table they change goes after the file's end, its offset moved there. */
		void ApplyNameOverrides(const FCdImage& Image, std::vector<uint8>& File)
		{
			const uint32 Count = File.size() >= 4 ? LittleEndian::ReadU32(File, 0) : 0;
			for (uint32 Table = 0; Table < Count && 4 + 4 * (static_cast<std::size_t>(Table) + 1) <= File.size(); ++Table)
			{
				const uint32 Offset = LittleEndian::ReadU32(File, 4 + 4 * static_cast<std::size_t>(Table));
				if (static_cast<int32>(Table) == FMesNameTables::GlyphMapTable || Offset >= File.size())
				{
					continue;
				}
				std::optional<FAppliedMessages> Applied = ApplyMessageOverrides(Image, MessageTables::Names(static_cast<int32>(Table)),
					std::span<const uint8>(File).subspan(Offset), EMessageWindows::Program);
				if (Applied)
				{
					const uint32 End = static_cast<uint32>(File.size());
					File.insert(File.end(), Applied->Table.begin(), Applied->Table.end());
					LittleEndian::WriteU32(File, 4 + 4 * static_cast<std::size_t>(Table), End);
				}
			}
		}
	}

	TResult<FMesResident> LoadMesResident(const FCdImage& Image, const FGameFileIndex& Index, const FPsxExecutable& Executable)
	{
		const TResult<std::vector<uint8>> Font = ReadPacked(Image, Index, FontFile);
		if (!Font)
		{
			return Font.Error();
		}
		TResult<std::vector<uint8>> Names = ReadPacked(Image, Index, NameTablesFile);
		if (!Names)
		{
			return Names.Error();
		}
		ApplyNameOverrides(Image, *Names);
		const std::span<const uint8> Box = Executable.Read(BoxGlyphAddress, FMesFont::GlyphBytes);
		TResult<FMesFont> ParsedFont = FMesFont::Parse(*Font, Box);
		if (!ParsedFont)
		{
			return ParsedFont.Error();
		}
		if (const std::shared_ptr<const FMesFontSheet> Sheet = Image.FontSheet())
		{
			Sheet->DrawInto(*ParsedFont);
		}
		TResult<FMesNameTables> ParsedNames = FMesNameTables::Parse(std::move(*Names));
		if (!ParsedNames)
		{
			return ParsedNames.Error();
		}
		return FMesResident{std::move(*ParsedFont), std::move(*ParsedNames)};
	}
}
