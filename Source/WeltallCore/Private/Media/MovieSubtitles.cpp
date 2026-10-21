#include "Media/MovieSubtitles.h"

#include "Foundation/LittleEndian.h"
#include "Foundation/Sha1.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace Weltall
{
	namespace
	{
		/**
		 * Where the undub keeps the table, below the end of the index's copy: the base of the directory its files
		 * are counted in, and the movies' pairs of entry and record, which a null entry ends.
		 */
		constexpr uint32 DirectoryBaseAddress = 0x80010A74;
		constexpr uint32 MoviesAddress = 0x80017208;
		constexpr uint32 IndexEnd = 0x80018000;
		/** The patch's code in the index's spare slots, and its SHA-1 on both of the undub's discs. */
		constexpr uint32 CodeAddress = 0x800107AC;
		constexpr uint32 CodeEnd = 0x80010A54;
		constexpr std::string_view UndubCode = "f90b967723d5dae0532179595fbb4918980ea3b5";
		constexpr uint32 PairBytes = 8;
		constexpr uint32 EntryBytes = 16;
		constexpr uint32 LineBytes = 0x18;
		/** The patch looks only at the movie directory's files, whose base is 0: movie n, file n + 2, is record n + 1. */
		constexpr int32 FirstMovieRecord = 1;

		/** A line draws into the decoded picture's 16-pixel columns of 24-bit colour, as the player uploads them. */
		constexpr int32 ColumnPixels = 16;
		constexpr uint32 ColumnRowBytes = ColumnPixels / 2;
		constexpr uint32 StripRowBytes = ColumnPixels * 3;
		/** Every movie of both discs is 320 x 224. */
		constexpr int32 MovieWidth = 320;
		constexpr int32 MovieHeight = 224;

		constexpr std::size_t PaletteBytes = 16 * 4;
		constexpr uint32 Opaque = 128;

		/** A line as the patch reads it; one that would draw across its strip's rows or past the picture is left out (the supported discs have none). */
		std::optional<FMovieSubtitleLine> ReadLine(std::span<const uint8> Bytes)
		{
			const uint32 ColumnBytes = LittleEndian::ReadU16(Bytes, 4);
			const uint32 StripOffset = LittleEndian::ReadU16(Bytes, 6);
			const int32 FirstColumn = LittleEndian::ReadU16(Bytes, 0xA);
			const int32 EndColumn = LittleEndian::ReadU16(Bytes, 0xC);
			FMovieSubtitleLine Line;
			Line.FirstFrame = LittleEndian::ReadU16(Bytes, 0x10);
			Line.EndFrame = LittleEndian::ReadU16(Bytes, 0x12);
			Line.X = FirstColumn * ColumnPixels;
			Line.Y = static_cast<int32>(StripOffset / StripRowBytes);
			Line.Width = (EndColumn - FirstColumn) * ColumnPixels;
			Line.Height = static_cast<int32>(ColumnBytes / ColumnRowBytes);
			Line.Offset = LittleEndian::ReadU32(Bytes, 0);
			if (ColumnBytes % ColumnRowBytes != 0 || StripOffset % StripRowBytes != 0 || FirstColumn >= EndColumn || Line.X + Line.Width > MovieWidth ||
				Line.Height == 0 || Line.Y + Line.Height > MovieHeight)
			{
				return std::nullopt;
			}
			return Line;
		}

		uint8 Premultiplied(uint8 Colour, uint32 Alpha)
		{
			return static_cast<uint8>((Colour * Alpha + Opaque / 2) / Opaque);
		}
	}

	FMovieSubtitleTable FMovieSubtitleTable::Read(const FPsxExecutable& Executable)
	{
		FMovieSubtitleTable Table;
		const std::span<const uint8> Base = Executable.Read(DirectoryBaseAddress, 4);
		if (Base.empty())
		{
			return Table;
		}
		const uint32 DirectoryBase = LittleEndian::ReadU32(Base, 0);
		for (uint32 Pair = MoviesAddress; Pair + PairBytes <= IndexEnd; Pair += PairBytes)
		{
			const std::span<const uint8> PairData = Executable.Read(Pair, PairBytes);
			const uint32 EntryAddress = PairData.empty() ? 0 : LittleEndian::ReadU32(PairData, 0);
			if (EntryAddress == 0)
			{
				break;
			}
			const std::span<const uint8> Entry = Executable.Read(EntryAddress, EntryBytes);
			if (Entry.empty())
			{
				continue;
			}
			const uint32 FirstLine = LittleEndian::ReadU32(Entry, 4);
			const uint32 EndLine = LittleEndian::ReadU32(Entry, 8);
			if (EndLine <= FirstLine || (EndLine - FirstLine) % LineBytes != 0)
			{
				continue;
			}
			const std::span<const uint8> Lines = Executable.Read(FirstLine, EndLine - FirstLine);
			if (Lines.empty())
			{
				continue;
			}
			FMovieSubtitleEntry Movie;
			Movie.Movie = static_cast<int32>(LittleEndian::ReadU32(PairData, 4)) - FirstMovieRecord;
			Movie.Record = static_cast<std::size_t>(DirectoryBase) + LittleEndian::ReadU32(Entry, 0) - 1;
			for (std::size_t Offset = 0; Offset < Lines.size(); Offset += LineBytes)
			{
				if (const std::optional<FMovieSubtitleLine> Line = ReadLine(Lines.subspan(Offset, LineBytes)))
				{
					Movie.Lines.push_back(*Line);
				}
			}
			Table.Movies.push_back(std::move(Movie));
		}
		return Table;
	}

	bool FMovieSubtitleTable::HasUndubCode(const FPsxExecutable& Executable)
	{
		const std::span<const uint8> Code = Executable.Read(CodeAddress, CodeEnd - CodeAddress);
		return !Code.empty() && FSha1::ToHex(FSha1::Hash(Code)) == UndubCode;
	}

	const FMovieSubtitleEntry* FMovieSubtitleTable::Find(int32 Movie) const
	{
		// The patch takes the first entry for the record, as this does.
		const auto Found = std::find_if(Movies.begin(), Movies.end(), [Movie](const FMovieSubtitleEntry& Entry) { return Entry.Movie == Movie; });
		return Found != Movies.end() ? &*Found : nullptr;
	}

	TResult<FMovieSubtitles> FMovieSubtitles::Load(const FCdImage& Image, const FGameFileIndex& Index, const FMovieSubtitleEntry& Entry)
	{
		if (Entry.Record >= Index.Records().size() || !Index.Records()[Entry.Record].IsFile())
		{
			return MakeError("movie " + std::to_string(Entry.Movie) + "'s subtitles name a file the disc does not have");
		}
		const FIndexRecord& Record = Index.Records()[Entry.Record];
		const TResult<std::vector<uint8>> Sectors = ReadFileSectors(Image, Record);
		if (!Sectors)
		{
			return Sectors.Error();
		}
		return FromFile(std::span<const uint8>(*Sectors).first(static_cast<std::size_t>(Record.Size)), Entry.Lines);
	}

	TResult<FMovieSubtitles> FMovieSubtitles::FromFile(std::span<const uint8> File, std::span<const FMovieSubtitleLine> Lines)
	{
		if (File.size() < PaletteBytes)
		{
			return MakeError("a movie's subtitle file is too short for its colours");
		}
		FMovieSubtitles Subtitles;
		for (const FMovieSubtitleLine& Line : Lines)
		{
			const std::size_t ColumnBytes = static_cast<std::size_t>(Line.Height) * ColumnRowBytes;
			const int32 Columns = Line.Width / ColumnPixels;
			if (Line.Offset + Columns * ColumnBytes > File.size())
			{
				return MakeError("a movie's subtitle line lies past the end of its file");
			}
			auto Picture = std::make_shared<FMovieSubtitlePicture>();
			Picture->X = Line.X;
			Picture->Y = Line.Y;
			Picture->Width = Line.Width;
			Picture->Height = Line.Height;
			Picture->Pixels.resize(static_cast<std::size_t>(Line.Width) * Line.Height * 4);
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const std::span<const uint8> Source = File.subspan(Line.Offset + Column * ColumnBytes, ColumnBytes);
				for (int32 Pixel = 0; Pixel < Line.Height * ColumnPixels; ++Pixel)
				{
					const std::size_t Colour = static_cast<std::size_t>((Source[static_cast<std::size_t>(Pixel / 2)] >> (Pixel % 2 * 4)) & 0xF) * 4;
					const uint32 Alpha = std::min<uint32>(File[Colour + 3], Opaque);
					const int32 X = Column * ColumnPixels + Pixel % ColumnPixels;
					uint8* Out = &Picture->Pixels[(static_cast<std::size_t>(Pixel / ColumnPixels) * Line.Width + X) * 4];
					Out[0] = Premultiplied(File[Colour + 2], Alpha);
					Out[1] = Premultiplied(File[Colour + 1], Alpha);
					Out[2] = Premultiplied(File[Colour], Alpha);
					Out[3] = static_cast<uint8>((Alpha * 255 + Opaque / 2) / Opaque);
				}
			}
			Subtitles.Lines.push_back(Line);
			Subtitles.Pictures.push_back(std::move(Picture));
		}
		return Subtitles;
	}

	std::shared_ptr<const FMovieSubtitlePicture> FMovieSubtitles::At(uint32 Frame)
	{
		while (NextLine < Lines.size() && Frame >= Lines[NextLine].EndFrame)
		{
			++NextLine;
		}
		if (NextLine == Lines.size() || Frame < Lines[NextLine].FirstFrame)
		{
			return nullptr;
		}
		return Pictures[NextLine];
	}
}
