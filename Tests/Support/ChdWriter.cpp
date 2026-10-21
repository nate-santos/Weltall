#include "Support/ChdWriter.h"

#include "Foundation/Sha1.h"

#include <algorithm>
#include <array>
#include <string>

namespace Weltall::Testing
{
	namespace
	{
		constexpr uint32 HeaderBytes = 124;
		constexpr uint32 MetadataOffset = HeaderBytes;
		constexpr uint32 MetadataHeaderBytes = 16;
		constexpr std::size_t CdSectorBytes = 2352;
		constexpr std::size_t CdFrameBytes = 2448;

		void WriteBigEndian64(std::vector<uint8>& Bytes, std::size_t Offset, uint64 Value)
		{
			WriteBigEndian32(Bytes, Offset, static_cast<uint32>(Value >> 32));
			WriteBigEndian32(Bytes, Offset + 4, static_cast<uint32>(Value));
		}

		uint8 Bcd(uint32 Value)
		{
			return static_cast<uint8>(Value / 10 << 4 | Value % 10);
		}

		/** The header, one metadata entry, each hunk with data at a whole number of hunks into the file, then the plain map. */
		std::vector<uint8> WritePlainChd(std::span<const uint8> Image, uint32 HunkBytes, uint32 UnitBytes, std::string_view Tag, std::span<const uint8> Metadata)
		{
			std::vector<uint8> Chd(HunkBytes, 0);
			constexpr std::string_view Signature = "MComprHD";
			std::copy(Signature.begin(), Signature.end(), Chd.begin());
			WriteBigEndian32(Chd, 8, HeaderBytes);
			WriteBigEndian32(Chd, 12, 5);
			WriteBigEndian64(Chd, 32, Image.size());
			WriteBigEndian64(Chd, 48, MetadataOffset);
			WriteBigEndian32(Chd, 56, HunkBytes);
			WriteBigEndian32(Chd, 60, UnitBytes);
			const FSha1::FDigest RawSha1 = FSha1::Hash(Image);
			std::copy(RawSha1.begin(), RawSha1.end(), Chd.begin() + 64);
			// The metadata entry: its tag, a flag, its length, no entry after it, then its data.
			std::copy(Tag.begin(), Tag.end(), Chd.begin() + MetadataOffset);
			WriteBigEndian32(Chd, MetadataOffset + 4, 0x01000000 | static_cast<uint32>(Metadata.size()));
			std::copy(Metadata.begin(), Metadata.end(), Chd.begin() + MetadataOffset + MetadataHeaderBytes);

			const std::size_t HunkCount = (Image.size() + HunkBytes - 1) / HunkBytes;
			std::vector<uint32> Map(HunkCount, 0);
			for (std::size_t Hunk = 0; Hunk < HunkCount; ++Hunk)
			{
				// The last hunk's padding is zeros: a hunk is all zeros when its piece of the image is.
				const std::span<const uint8> Piece = Image.subspan(Hunk * HunkBytes, std::min<std::size_t>(HunkBytes, Image.size() - Hunk * HunkBytes));
				if (std::all_of(Piece.begin(), Piece.end(), [](uint8 Byte) { return Byte == 0; }))
				{
					continue;
				}
				const std::size_t At = Chd.size();
				Map[Hunk] = static_cast<uint32>(At / HunkBytes);
				Chd.resize(At + HunkBytes, 0);
				std::copy(Piece.begin(), Piece.end(), Chd.begin() + static_cast<std::ptrdiff_t>(At));
			}
			WriteBigEndian64(Chd, 40, Chd.size());
			const std::size_t MapOffset = Chd.size();
			Chd.resize(MapOffset + HunkCount * 4);
			for (std::size_t Hunk = 0; Hunk < HunkCount; ++Hunk)
			{
				WriteBigEndian32(Chd, MapOffset + Hunk * 4, Map[Hunk]);
			}
			return Chd;
		}
	}

	void WriteBigEndian32(std::vector<uint8>& Bytes, std::size_t Offset, uint32 Value)
	{
		for (std::size_t Index = 0; Index < 4; ++Index)
		{
			Bytes[Offset + Index] = static_cast<uint8>(Value >> (24 - 8 * Index));
		}
	}

	std::vector<uint8> MakePlainChd(std::span<const uint8> Image, uint32 HunkBytes, uint32 UnitBytes)
	{
		constexpr std::array<uint8, 1> Dvd{0};
		return WritePlainChd(Image, HunkBytes, UnitBytes, "DVD ", Dvd);
	}

	std::vector<uint8> MakePlainCdChd(std::span<const uint8> Bin, std::string_view TrackType)
	{
		// chdman's default: eight frames a hunk; the track's frames padded to a whole number of four.
		const std::size_t Frames = Bin.size() / CdSectorBytes;
		std::vector<uint8> Units((Frames + 3) / 4 * 4 * CdFrameBytes, 0);
		for (std::size_t Frame = 0; Frame < Frames; ++Frame)
		{
			std::copy_n(Bin.begin() + static_cast<std::ptrdiff_t>(Frame * CdSectorBytes), CdSectorBytes, Units.begin() + static_cast<std::ptrdiff_t>(Frame * CdFrameBytes));
		}
		const std::string Track = "TRACK:1 TYPE:" + std::string(TrackType) + " SUBTYPE:NONE FRAMES:" + std::to_string(Frames)
			+ " PREGAP:0 PGTYPE:MODE1 PGSUB:NONE POSTGAP:0";
		std::vector<uint8> Text(Track.begin(), Track.end());
		Text.push_back(0);
		return WritePlainChd(Units, 8 * CdFrameBytes, CdFrameBytes, "CHT2", Text);
	}

	std::vector<uint8> RawMode2Sectors(std::span<const uint8> Iso)
	{
		constexpr std::size_t DataBytes = 2048;
		const std::size_t Count = Iso.size() / DataBytes;
		std::vector<uint8> Bin(Count * CdSectorBytes, 0);
		for (std::size_t Sector = 0; Sector < Count; ++Sector)
		{
			uint8* Raw = Bin.data() + Sector * CdSectorBytes;
			std::fill_n(Raw + 1, 10, uint8{0xFF});
			const uint32 Address = static_cast<uint32>(Sector) + 150;
			Raw[12] = Bcd(Address / 4500);
			Raw[13] = Bcd(Address / 75 % 60);
			Raw[14] = Bcd(Address % 75);
			Raw[15] = 2;
			Raw[18] = Raw[22] = 0x08;
			std::copy_n(Iso.begin() + static_cast<std::ptrdiff_t>(Sector * DataBytes), DataBytes, Raw + 24);
		}
		return Bin;
	}

	std::vector<uint8> RawSector(const FCdSectorHeader& Header, std::span<const uint8> UserData)
	{
		std::vector<uint8> Sector(CdSectorBytes, 0);
		std::fill_n(Sector.begin() + 1, 10, uint8{0xFF});
		Sector[15] = Header.Mode;
		const std::array<uint8, 4> Subheader{Header.FileNumber, Header.Channel, Header.Submode, Header.CodingInfo};
		std::copy(Subheader.begin(), Subheader.end(), Sector.begin() + 16);
		std::copy(Subheader.begin(), Subheader.end(), Sector.begin() + 20);
		std::copy(UserData.begin(), UserData.end(), Sector.begin() + 24);
		return Sector;
	}
}
