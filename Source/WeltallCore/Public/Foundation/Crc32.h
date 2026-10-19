#pragma once

#include "Foundation/Types.h"

#include <span>

namespace Weltall
{
	/** CRC-32 as zlib computes it (the reflected polynomial EDB88320h), continuing from Crc: 0 to begin. */
	WELTALLCORE_API uint32 Crc32(std::span<const uint8> Bytes, uint32 Crc = 0);

	/** The same over 16-bit units, each taken as its two bytes low first, as they lie in the console's memory. */
	WELTALLCORE_API uint32 Crc32OfUnits(std::span<const uint16> Units, uint32 Crc = 0);
}
