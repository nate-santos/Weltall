#include "Media/GameFileIndex.h"
#include "Media/MovieSubtitles.h"
#include "Media/PsxExecutable.h"
#include "Support/ByteWriter.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cstring>

using namespace Weltall;
using Testing::FByteWriter;

namespace
{
	constexpr uint32 LoadAddress = 0x80010000;

	/** An executable holding the index's copy and the directory table after it, with Bytes written at each address. */
	FPsxExecutable MakeExecutable(std::initializer_list<std::pair<uint32, const FByteWriter*>> Parts)
	{
		std::vector<uint8> Body(0x8100, 0);
		for (const auto& [Address, Bytes] : Parts)
		{
			std::memcpy(Body.data() + (Address - LoadAddress), Bytes->Bytes.data(), Bytes->Size());
		}
		std::vector<uint8> File(FPsxExecutable::HeaderSize, 0);
		std::memcpy(File.data(), "PS-X EXE", 8);
		const uint32 Fields[] = {LoadAddress, 0, LoadAddress, static_cast<uint32>(Body.size())};
		std::memcpy(File.data() + 0x10, Fields, sizeof(Fields));
		File.insert(File.end(), Body.begin(), Body.end());
		return *FPsxExecutable::Parse(std::move(File));
	}

	/** A line as the undub's table holds it. */
	FByteWriter& Line(FByteWriter& Writer, uint32 Offset, uint16 ColumnBytes, uint16 StripOffset, uint16 FirstColumn, uint16 EndColumn, uint16 FirstFrame,
		uint16 EndFrame)
	{
		return Writer.U32(Offset).U16(ColumnBytes).U16(StripOffset).U16(static_cast<uint16>(ColumnBytes / 8 * 48)).U16(FirstColumn).U16(EndColumn).U16(0).U16(FirstFrame).U16(EndFrame)
			.U16(FirstFrame).U16(EndFrame);
	}

	/** A subtitle file: its 16 colours (blue, green, red, alpha out of 128), then Pixels. */
	std::vector<uint8> SubtitleFile(std::initializer_list<std::array<uint8, 4>> Colours, std::initializer_list<uint8> Pixels)
	{
		std::vector<uint8> File(64, 0);
		std::size_t Offset = 0;
		for (const auto& Colour : Colours)
		{
			std::memcpy(File.data() + Offset, Colour.data(), 4);
			Offset += 4;
		}
		File.insert(File.end(), Pixels);
		return File;
	}

	std::array<uint8, 4> PixelAt(const FMovieSubtitlePicture& Picture, int32 X, int32 Y)
	{
		const std::size_t At = (static_cast<std::size_t>(Y) * Picture.Width + X) * 4;
		return {Picture.Pixels[At], Picture.Pixels[At + 1], Picture.Pixels[At + 2], Picture.Pixels[At + 3]};
	}
}

TEST_CASE("The undub's subtitle table names each movie by the record its lookup reaches and reads its lines as the patch does", "[Media]")
{
	FByteWriter Base;
	Base.U32(4150);
	FByteWriter Pairs;
	Pairs.U32(0x80017300).U32(14).U32(0x80017380).U32(3).U32(0x800173C0).U32(4).U32(0).U32(0);
	FByteWriter Opening;
	Opening.U32(8).U32(0x80017310).U32(0x80017340).U32(0x80194000);
	Line(Opening, 64, 112, 189 * 48, 2, 18, 931, 971);
	// Rows that would start inside a strip's pixel draw out of place: the line is left out.
	Line(Opening, 1856, 112, 189 * 48 + 1, 2, 18, 975, 990);
	FByteWriter Second;
	Second.U32(2).U32(0x80017390).U32(0x800173A8).U32(0x80194000);
	Line(Second, 64, 8, 0, 19, 20, 5, 9);
	FByteWriter Empty;
	Empty.U32(3).U32(0x800173D0).U32(0x800173D0).U32(0x80194000);
	const FPsxExecutable Executable = MakeExecutable({{0x80010A74, &Base}, {0x80017208, &Pairs}, {0x80017300, &Opening}, {0x80017380, &Second},
		{0x800173C0, &Empty}});

	const FMovieSubtitleTable Table = FMovieSubtitleTable::Read(Executable);
	REQUIRE(Table.Entries().size() == 2);
	const FMovieSubtitleEntry* Movie = Table.Find(13);
	REQUIRE(Movie != nullptr);
	CHECK(Movie->Record == 4157);
	REQUIRE(Movie->Lines.size() == 1);
	CHECK(Movie->Lines[0] == FMovieSubtitleLine{931, 971, 32, 189, 256, 14, 64});
	const FMovieSubtitleEntry* Other = Table.Find(2);
	REQUIRE(Other != nullptr);
	CHECK(Other->Record == 4151);
	CHECK(Other->Lines[0] == FMovieSubtitleLine{5, 9, 304, 0, 16, 1, 64});
	CHECK(Table.Find(3) == nullptr);
	CHECK(Table.Find(0) == nullptr);

	// Only the supported undub's executables are read for it.
	const auto Index = FGameFileIndex::Read(Executable);
	REQUIRE(Index);
	CHECK(Index->MovieSubtitles().Entries().empty());
}

TEST_CASE("A subtitle line's pixels take the file's colours, low nibble first, premultiplied by an alpha out of 128", "[Media]")
{
	// Column 0's first byte: colour 1, then colour 2; column 1's: colour 3, then the clear colour 0.
	std::initializer_list<uint8> Pixels = {0x21, 0, 0, 0, 0, 0, 0, 0, 0x03, 0, 0, 0, 0, 0, 0, 0};
	const std::vector<uint8> File = SubtitleFile({{0, 0, 0, 0}, {10, 20, 200, 128}, {255, 255, 255, 64}, {0, 0, 0, 200}}, Pixels);
	const std::array<FMovieSubtitleLine, 1> Lines{FMovieSubtitleLine{1, 2, 16, 3, 32, 1, 64}};
	TResult<FMovieSubtitles> Subtitles = FMovieSubtitles::FromFile(File, Lines);
	REQUIRE(Subtitles);
	const auto Picture = Subtitles->At(1);
	REQUIRE(Picture);
	CHECK(Picture->X == 16);
	CHECK(Picture->Y == 3);
	CHECK(Picture->Width == 32);
	CHECK(Picture->Height == 1);
	CHECK(PixelAt(*Picture, 0, 0) == std::array<uint8, 4>{200, 20, 10, 255});
	CHECK(PixelAt(*Picture, 1, 0) == std::array<uint8, 4>{128, 128, 128, 128});
	// An alpha past 128 covers the movie, as 128 does.
	CHECK(PixelAt(*Picture, 16, 0) == std::array<uint8, 4>{0, 0, 0, 255});
	CHECK(PixelAt(*Picture, 17, 0) == std::array<uint8, 4>{0, 0, 0, 0});

	CHECK_FALSE(FMovieSubtitles::FromFile(std::span<const uint8>(File).first(63), {}));
	const std::array<FMovieSubtitleLine, 1> PastTheEnd{FMovieSubtitleLine{1, 2, 0, 0, 48, 1, 64}};
	CHECK_FALSE(FMovieSubtitles::FromFile(File, PastTheEnd));
}

TEST_CASE("Subtitle lines only go forward until the movie's stream starts again", "[Media]")
{
	const std::vector<uint8> File = SubtitleFile({}, {0, 0, 0, 0, 0, 0, 0, 0});
	const std::array<FMovieSubtitleLine, 2> Lines{FMovieSubtitleLine{10, 20, 0, 0, 16, 1, 64}, FMovieSubtitleLine{30, 40, 0, 0, 16, 1, 64}};
	TResult<FMovieSubtitles> Subtitles = FMovieSubtitles::FromFile(File, Lines);
	REQUIRE(Subtitles);
	CHECK(Subtitles->At(9) == nullptr);
	const auto First = Subtitles->At(10);
	REQUIRE(First);
	CHECK(Subtitles->At(19) == First);
	CHECK(Subtitles->At(20) == nullptr);
	const auto Second = Subtitles->At(30);
	REQUIRE(Second);
	CHECK(Second != First);
	// A line once passed is not drawn again, even for an earlier frame.
	CHECK(Subtitles->At(15) == nullptr);
	CHECK(Subtitles->At(40) == nullptr);
	Subtitles->Restart();
	CHECK(Subtitles->At(15) == First);
}
