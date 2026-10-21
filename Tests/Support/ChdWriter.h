#pragma once

#include "Foundation/Types.h"
#include "Media/CdImage.h"

#include <span>
#include <string_view>
#include <vector>

namespace Weltall::Testing
{
	/**
	 * A CHD that packs nothing, as `chdman createdvd -c none` writes one: the header, a DVD's metadata, the image's hunks
	 * in place (those of zeros left out) and a plain map. Enough to put a test's disc in a CHD without chdman.
	 */
	std::vector<uint8> MakePlainChd(std::span<const uint8> Image, uint32 HunkBytes = 4096, uint32 UnitBytes = 2048);

	/**
	 * A CHD that packs nothing of a CD of one track: each of the BIN's raw sectors followed by subchannel data of
	 * zeros, the track's metadata naming its type ("MODE2_RAW", "AUDIO").
	 */
	std::vector<uint8> MakePlainCdChd(std::span<const uint8> Bin, std::string_view TrackType);

	/** An ISO's sectors of 2048 bytes as the raw Mode 2 Form 1 sectors of a CD's BIN, their error codes left as zeros. */
	std::vector<uint8> RawMode2Sectors(std::span<const uint8> Iso);

	/** A raw sector of the header's mode and subheader holding the user data, its address and error codes left as zeros. */
	std::vector<uint8> RawSector(const FCdSectorHeader& Header, std::span<const uint8> UserData);

	/** Where the fields a test changes lie in a CHD's header. */
	namespace ChdHeader
	{
		constexpr std::size_t Version = 12;
		constexpr std::size_t SecondCodec = 20;
		constexpr std::size_t HunkBytes = 56;
		constexpr std::size_t ParentSha1 = 104;
	}

	/** Writes a big-endian 32-bit value, as a CHD's header keeps them. */
	void WriteBigEndian32(std::vector<uint8>& Bytes, std::size_t Offset, uint32 Value);
}
