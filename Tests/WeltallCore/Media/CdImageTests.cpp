#include "Media/CdImage.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace Weltall;

TEST_CASE("Source opening rejects invalid tracks and retains 64-bit offsets", "[Media][PackagedDiscs]")
{
	CHECK_FALSE(FCdImage::OpenSource(nullptr));
	auto Small = std::make_shared<FMemoryByteSource>(std::vector<uint8>(2352));
	CHECK_FALSE(FCdImage::OpenSource(Small, 2352, 2353));
	CHECK_FALSE(FCdImage::OpenSource(Small, 17));
	class FLargeSource final : public IByteSource
	{
	public:
		uint64 Size() const override { return (uint64{1} << 32) + 2 * CdSector::RawSize; }
		bool Read(uint64 Offset, std::span<uint8> Destination) const override
		{
			if (Offset > Size() || Destination.size() > Size() - Offset) return false;
			for (std::size_t I = 0; I < Destination.size(); ++I) Destination[I] = static_cast<uint8>((Offset + I) % 251);
			return true;
		}
	};
	auto Image = FCdImage::OpenSource(std::make_shared<FLargeSource>(), 2352, uint64{1} << 32);
	REQUIRE(Image);
	CHECK(Image->SectorCount() == 2);
	const auto Sector = Image->ReadSourceSector(1);
	REQUIRE(Sector);
	CHECK((*Sector)[0] == static_cast<uint8>(((uint64{1} << 32) + 2352) % 251));
	CHECK_FALSE(Image->ReadSourceSector(2));
}

namespace
{
	/** A raw Mode 2 sector whose user data is filled with Value. */
	std::vector<uint8> RawSector(uint8 Value, bool bForm2)
	{
		std::vector<uint8> Sector(CdSector::RawSize, 0);
		std::memset(Sector.data() + 1, 0xFF, 10);
		Sector[15] = 2;
		Sector[16] = 1;
		Sector[17] = 3;
		Sector[18] = bForm2 ? (CdSector::SubmodeAudio | CdSector::SubmodeForm2) : CdSector::SubmodeData;
		Sector[20] = Sector[16];
		Sector[21] = Sector[17];
		Sector[22] = Sector[18];
		std::memset(Sector.data() + 24, Value, bForm2 ? CdSector::Form2DataSize : CdSector::Form1DataSize);
		return Sector;
	}

	FCdImage MakeImage(std::initializer_list<std::vector<uint8>> Sectors)
	{
		std::vector<uint8> Bytes;
		for (const auto& Sector : Sectors)
		{
			Bytes.insert(Bytes.end(), Sector.begin(), Sector.end());
		}
		auto Image = FCdImage::OpenSource(std::make_shared<FMemoryByteSource>(std::move(Bytes)), CdSector::RawSize, 0);
		REQUIRE(Image);
		return std::move(*Image);
	}
}

TEST_CASE("Raw sectors yield Form 1 and Form 2 user data", "[Media]")
{
	const FCdImage Image = MakeImage({RawSector(0x11, false), RawSector(0x22, true)});
	REQUIRE(Image.SectorCount() == 2);
	const auto Form1 = Image.ReadUserData(0);
	REQUIRE(Form1);
	CHECK(Form1->size() == CdSector::Form1DataSize);
	CHECK((*Form1)[0] == 0x11);
	const auto Header = Image.ReadHeader(1);
	REQUIRE(Header);
	CHECK(Header->IsForm2());
	CHECK(Header->FileNumber == 1);
	CHECK(Header->Channel == 3);
	const auto Form2 = Image.ReadUserData(1);
	REQUIRE(Form2);
	CHECK(Form2->size() == CdSector::Form2DataSize);
	CHECK((*Form2)[CdSector::Form2DataSize - 1] == 0x22);
}

TEST_CASE("Form 1 reads span sectors and stop at the requested size", "[Media]")
{
	const FCdImage Image = MakeImage({RawSector(0x01, false), RawSector(0x02, false)});
	const auto Data = Image.ReadForm1(0, CdSector::Form1DataSize + 3);
	REQUIRE(Data);
	REQUIRE(Data->size() == CdSector::Form1DataSize + 3);
	CHECK((*Data)[CdSector::Form1DataSize - 1] == 0x01);
	CHECK((*Data)[CdSector::Form1DataSize] == 0x02);
	CHECK_FALSE(Image.ReadForm1(1, 2 * CdSector::Form1DataSize));
}

TEST_CASE("Form 1 reads take each sector's user data where its mode puts it", "[Media]")
{
	std::vector<uint8> Mode1(CdSector::RawSize, 0);
	std::memset(Mode1.data() + 1, 0xFF, 10);
	Mode1[15] = 1;
	std::memset(Mode1.data() + 16, 0x33, CdSector::Form1DataSize);
	const FCdImage Image = MakeImage({Mode1, RawSector(0x44, false)});
	const auto Data = Image.ReadForm1(0, 2 * CdSector::Form1DataSize);
	REQUIRE(Data);
	REQUIRE(Data->size() == 2 * CdSector::Form1DataSize);
	CHECK((*Data)[0] == 0x33);
	CHECK((*Data)[CdSector::Form1DataSize - 1] == 0x33);
	CHECK((*Data)[CdSector::Form1DataSize] == 0x44);
	CHECK((*Data)[2 * CdSector::Form1DataSize - 1] == 0x44);
	CHECK(Image.ReadForm1(5, 0));
	CHECK_FALSE(Image.ReadForm1(2, 1));
}

TEST_CASE("Form 1 reads of a cooked image take its sectors whole", "[Media]")
{
	std::vector<uint8> Bytes(3 * CdSector::CookedSize);
	for (std::size_t Index = 0; Index < Bytes.size(); ++Index)
	{
		Bytes[Index] = static_cast<uint8>(Index / CdSector::CookedSize + 1);
	}
	const FCdImage Image(std::make_shared<FMemoryByteSource>(std::move(Bytes)), CdSector::CookedSize, 0);
	const auto Data = Image.ReadForm1(1, CdSector::Form1DataSize + 5);
	REQUIRE(Data);
	REQUIRE(Data->size() == CdSector::Form1DataSize + 5);
	CHECK((*Data)[0] == 2);
	CHECK((*Data)[CdSector::Form1DataSize + 4] == 3);
	CHECK_FALSE(Image.ReadForm1(2, CdSector::Form1DataSize + 1));
}

TEST_CASE("Raw sectors kept with their subchannel read as the sectors alone", "[Media]")
{
	// Each sector followed by the 96 bytes of its subchannel, as some rippers keep them: on its own and through a sheet.
	const std::filesystem::path Folder = std::filesystem::temp_directory_path() / "WeltallSubchannelImage";
	std::error_code Error;
	std::filesystem::remove_all(Folder, Error);
	std::filesystem::create_directories(Folder);
	{
		std::ofstream File(Folder / "image.img", std::ios::binary);
		const std::array<char, 96> Subchannel{};
		for (uint8 Value = 0; Value < 12; ++Value)
		{
			const std::vector<uint8> Raw = RawSector(Value, Value % 5 == 4);
			File.write(reinterpret_cast<const char*>(Raw.data()), static_cast<std::streamsize>(Raw.size()));
			File.write(Subchannel.data(), Subchannel.size());
		}
	}
	std::ofstream(Folder / "image.cue") << "FILE \"image.img\" BINARY\n  TRACK 01 MODE2/2448\n    INDEX 01 00:00:00\n";
	for (const char* Name : {"image.img", "image.cue"})
	{
		INFO(Name);
		const TResult<FCdImage> Image = FCdImage::Open(Folder / Name);
		INFO((Image ? std::string() : Image.Error().Message));
		REQUIRE(Image);
		CHECK(Image->IsRaw());
		REQUIRE(Image->SectorCount() == 12);
		for (uint8 Value = 0; Value < 12; ++Value)
		{
			const auto Header = Image->ReadHeader(Value);
			REQUIRE(Header);
			CHECK(Header->IsForm2() == (Value % 5 == 4));
			const auto Data = Image->ReadUserData(Value);
			REQUIRE(Data);
			CHECK(Data->front() == Value);
			CHECK(Data->back() == Value);
		}
		const auto Across = Image->ReadForm1(2, 2 * CdSector::Form1DataSize);
		REQUIRE(Across);
		CHECK((*Across)[CdSector::Form1DataSize - 1] == 2);
		CHECK((*Across)[CdSector::Form1DataSize] == 3);
	}
	std::filesystem::remove_all(Folder, Error);
}
