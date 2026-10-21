#include "Formats/StrBitstream.h"
#include "Foundation/ByteSource.h"
#include "Foundation/Sha1.h"
#include "Graphics/Mdec.h"
#include "Graphics/MovieCleanup.h"
#include "Graphics/MovieUpscale.h"
#include "Media/GameFileIndex.h"
#include "Media/MovieStream.h"
#include "Media/PsxExecutable.h"
#include "Support/ChdWriter.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using namespace Weltall;

namespace
{
	/** A movie's record: its size is the sector count times the 2336 bytes after a raw sector's header. */
	constexpr uint32 MovieSectorBytes = 2336;

	FIndexRecord MovieRecord(const FIso9660& Disc, std::size_t Record)
	{
		const auto Executable = ReadBootExecutable(Disc);
		REQUIRE(Executable);
		const auto Index = FGameFileIndex::Read(*Executable);
		REQUIRE(Index);
		REQUIRE(Record < Index->Records().size());
		return Index->Records()[Record];
	}

	/** Frame FrameNumber of the movie at Record, read from its video sectors alone. */
	FStrFrame ReadFrame(const FIso9660& Disc, std::size_t Record, uint32 FrameNumber)
	{
		const FIndexRecord Movie = MovieRecord(Disc, Record);
		FStrFrameAssembler Assembler;
		std::optional<FStrFrame> Found;
		for (uint32 Lba = Movie.Lba; !Found && Lba < Movie.Lba + static_cast<uint32>(Movie.Size) / MovieSectorBytes; ++Lba)
		{
			const auto Header = Disc.Image().ReadHeader(Lba);
			REQUIRE(Header);
			if ((Header->Submode & CdSector::SubmodeVideo) == 0)
			{
				continue;
			}
			const auto UserData = Disc.Image().ReadUserData(Lba);
			REQUIRE(UserData);
			const auto Sector = ParseStrSector(*UserData);
			if (!Sector || Sector->FrameNumber != FrameNumber)
			{
				continue;
			}
			Found = Assembler.Add(*UserData);
		}
		INFO("frame " << FrameNumber);
		REQUIRE(Found);
		return *Found;
	}

	std::string CodesDigest(const std::vector<uint16>& Codes)
	{
		std::vector<uint8> Bytes(Codes.size() * 2);
		for (std::size_t Index = 0; Index < Codes.size(); ++Index)
		{
			Bytes[Index * 2] = static_cast<uint8>(Codes[Index]);
			Bytes[Index * 2 + 1] = static_cast<uint8>(Codes[Index] >> 8);
		}
		return FSha1::ToHex(FSha1::Hash(Bytes));
	}

	/** Checks frames of a disc's movies against SHA-1s of the codes the game's library decoder writes for them, run in an emulated CPU. */
	void CheckAgainstGameDecoder(const FIso9660& Disc, std::initializer_list<std::tuple<std::size_t, uint32, const char*>> Frames)
	{
		for (const auto& [Record, FrameNumber, Digest] : Frames)
		{
			const FStrFrame Frame = ReadFrame(Disc, Record, FrameNumber);
			const auto Codes = DecodeStrBitstream(Frame.Bitstream, Frame.Width, Frame.Height);
			REQUIRE(Codes);
			CHECK(CodesDigest(*Codes) == Digest);
		}
	}
}

TEST_CASE("Disc 1 movie bitstreams expand exactly as the game's own decoder expands them", "[Formats][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	CheckAgainstGameDecoder(*Disc, {
		{1, 1, "e27d20de5608bffdf3cbabdedd1f6c1866cf2f50"},
		{1, 40, "8d905dc48a3c22dc6f088965e36a5ab6209bc18a"},
		{8, 30, "3a34f4befa6587bd8094a93a5c4ddc90b164665c"},
		{14, 100, "ff970a66420595eed61167b83189b3aef6c1c8bc"},
	});
}

TEST_CASE("Disc 2 movie bitstreams expand exactly as the game's own decoder expands them", "[Formats][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc2);
	CheckAgainstGameDecoder(*Disc, {
		{1, 50, "5e78915959f0cf419f8c197c4fa52684b87e90e4"},
		{5, 20, "998b720bef794019c7681aeb5fc833cc04bc1d45"},
		{12, 650, "85af13a048689cf56338de9778e07325dc3c1182"},
	});
}

TEST_CASE("A movie streams whole frames and all of its sound", "[Media][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	// The 16th movie: 137 frames of 320x224, and 176 sound sectors of 37800 Hz stereo.
	const FIndexRecord Movie = MovieRecord(*Disc, 16);
	FMovieStream Stream(Disc->Image(), Movie.Lba, static_cast<uint32>(Movie.Size) / MovieSectorBytes);
	std::vector<int16> Sound;
	uint32 Frames = 0;
	int64 LumaSum = 0;
	for (;;)
	{
		auto Frame = Stream.NextFrame(Sound);
		REQUIRE(Frame);
		if (!Frame->has_value())
		{
			break;
		}
		const FStrFrame& Next = **Frame;
		CHECK(Next.FrameNumber == ++Frames);
		REQUIRE(Next.Width == 320);
		REQUIRE(Next.Height == 224);
		const auto Codes = DecodeStrBitstream(Next.Bitstream, Next.Width, Next.Height);
		REQUIRE(Codes);
		const auto Picture = DecodeMdecPicture(*Codes, Next.Width, Next.Height, FMdecTables::Standard());
		REQUIRE(Picture);
		LumaSum += Picture->Luma[Picture->Luma.size() / 2 + 160];
	}
	CHECK(Frames == 137);
	// Each sector's 2016 frames of sound become 2352 at 44100 Hz.
	CHECK(Sound.size() == 176u * 2352u * 2u);
	CHECK(LumaSum != 0);
}

TEST_CASE("A movie from an image of 2048-byte sectors streams the same frames, without its sound", "[Media][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FIndexRecord Movie = MovieRecord(*Disc, 16);
	const uint32 Sectors = static_cast<uint32>(Movie.Size) / MovieSectorBytes;
	// The movie's sectors as a ripper of 2048-byte images keeps them: each sector's first 2048 bytes of user data.
	std::vector<uint8> Cooked;
	for (uint32 Sector = 0; Sector < Sectors; ++Sector)
	{
		const auto UserData = Disc->Image().ReadUserData(Movie.Lba + Sector);
		REQUIRE(UserData);
		Cooked.insert(Cooked.end(), UserData->begin(), UserData->begin() + CdSector::CookedSize);
	}
	const FCdImage Image(std::make_shared<FMemoryByteSource>(std::move(Cooked)), CdSector::CookedSize, 0);
	FMovieStream Raw(Disc->Image(), Movie.Lba, Sectors);
	FMovieStream FromCooked(Image, 0, Sectors);
	std::vector<int16> RawSound;
	std::vector<int16> CookedSound;
	uint32 Frames = 0;
	for (;;)
	{
		auto Expected = Raw.NextFrame(RawSound);
		auto Actual = FromCooked.NextFrame(CookedSound);
		REQUIRE(Expected);
		REQUIRE(Actual);
		REQUIRE(Expected->has_value() == Actual->has_value());
		if (!Expected->has_value())
		{
			break;
		}
		++Frames;
		CHECK((*Actual)->FrameNumber == (*Expected)->FrameNumber);
		const bool bSameBitstream = (*Actual)->Bitstream == (*Expected)->Bitstream;
		CHECK(bSameBitstream);
	}
	CHECK(Frames == 137);
	CHECK(CookedSound.empty());
}

TEST_CASE("A movie whose video sectors are marked as data, as a fan translation's re-encoded ones are, streams the same frames and sound", "[Media][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const FIndexRecord Movie = MovieRecord(*Disc, 16);
	const uint32 Sectors = static_cast<uint32>(Movie.Size) / MovieSectorBytes;
	// The Spanish translation's movies: file 0, real-time data with no coding info, where the release's say file 1, real-time video.
	std::vector<uint8> Bin;
	for (uint32 Sector = 0; Sector < Sectors; ++Sector)
	{
		auto Header = Disc->Image().ReadHeader(Movie.Lba + Sector);
		const auto UserData = Disc->Image().ReadUserData(Movie.Lba + Sector);
		REQUIRE(Header);
		REQUIRE(UserData);
		if ((Header->Submode & CdSector::SubmodeVideo) != 0)
		{
			*Header = {2, 0, Header->Channel, 0x48, 0};
		}
		const std::vector<uint8> Raw = Testing::RawSector(*Header, *UserData);
		Bin.insert(Bin.end(), Raw.begin(), Raw.end());
	}
	const FCdImage Image(std::make_shared<FMemoryByteSource>(std::move(Bin)), CdSector::RawSize, 0);
	FMovieStream Release(Disc->Image(), Movie.Lba, Sectors);
	FMovieStream Translated(Image, 0, Sectors);
	std::vector<int16> ReleaseSound;
	std::vector<int16> TranslatedSound;
	uint32 Frames = 0;
	for (;;)
	{
		auto Expected = Release.NextFrame(ReleaseSound);
		auto Actual = Translated.NextFrame(TranslatedSound);
		REQUIRE(Expected);
		REQUIRE(Actual);
		REQUIRE(Expected->has_value() == Actual->has_value());
		if (!Expected->has_value())
		{
			break;
		}
		++Frames;
		CHECK((*Actual)->FrameNumber == (*Expected)->FrameNumber);
		const bool bSameBitstream = (*Actual)->Bitstream == (*Expected)->Bitstream;
		CHECK(bSameBitstream);
	}
	CHECK(Frames == 137);
	CHECK(TranslatedSound == ReleaseSound);
}

TEST_CASE("A movie picture is cleaned within its quantization and upscales to the same pixels on every machine", "[Media][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	// The second movie's 30th frame, Fei before a stormy sky: line art, a face, a gradient.
	const FStrFrame Frame = ReadFrame(*Disc, 2, 30);
	const auto Codes = DecodeStrBitstream(Frame.Bitstream, Frame.Width, Frame.Height);
	REQUIRE(Codes);
	const auto Picture = DecodeMdecPicture(*Codes, Frame.Width, Frame.Height, FMdecTables::Standard());
	REQUIRE(Picture);
	REQUIRE(Picture->QuantScale == 2);
	FMdecPicture Cleaned = *Picture;
	CleanMoviePicture(Cleaned);
	// Each sample moves by at most the quantizer across the columns' block edges, again across the rows', and half of it in the deringing.
	const int32 Bound = 3 * Picture->QuantScale * (1 << MdecSampleFractionBits);
	int32 Largest = 0;
	int32 Changed = 0;
	for (std::size_t Index = 0; Index < Picture->Luma.size(); ++Index)
	{
		const int32 Change = std::abs(Cleaned.Luma[Index] - Picture->Luma[Index]);
		Largest = std::max(Largest, Change);
		Changed += Change != 0 ? 1 : 0;
	}
	CHECK(Largest <= Bound);
	CHECK(Changed > 0);

	const FMovieImage Upscaled = MovieUpscale::Upscale(Cleaned);
	REQUIRE(Upscaled.Width == 1280);
	REQUIRE(Upscaled.Height == 896);
	std::vector<uint8> Bytes;
	for (const FMovieTexel& Texel : Upscaled.Texels)
	{
		for (const int16 Sample : Texel)
		{
			Bytes.push_back(static_cast<uint8>(Sample));
			Bytes.push_back(static_cast<uint8>(static_cast<uint16>(Sample) >> 8));
		}
	}
	CHECK(FSha1::ToHex(FSha1::Hash(Bytes)) == "818122578a6e99261ef45800be8faffcbd90a77a");
}
