#include "Foundation/Crc32.h"

#include <catch_amalgamated.hpp>

#include <string_view>
#include <vector>

using namespace Weltall;

namespace
{
	uint32 CrcOfText(std::string_view Text)
	{
		return Crc32(std::span(reinterpret_cast<const uint8*>(Text.data()), Text.size()));
	}
}

TEST_CASE("CRC-32 matches zlib's check values", "[Foundation]")
{
	CHECK(CrcOfText("") == 0u);
	CHECK(CrcOfText("123456789") == 0xCBF43926u);
	CHECK(CrcOfText("The quick brown fox jumps over the lazy dog") == 0x414FA339u);
}

TEST_CASE("CRC-32 continues from a value so far, and reads units low byte first", "[Foundation]")
{
	std::vector<uint8> Bytes;
	for (int32 Index = 0; Index < 1001; ++Index)
	{
		Bytes.push_back(static_cast<uint8>(Index * 37 + 11));
	}
	const uint32 Whole = Crc32(Bytes);
	for (const std::size_t Split : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{8}, std::size_t{500}, std::size_t{1001}})
	{
		CHECK(Crc32(std::span(Bytes).subspan(Split), Crc32(std::span(Bytes).first(Split))) == Whole);
	}
	std::vector<uint16> Units;
	for (std::size_t Index = 0; Index + 1 < Bytes.size(); Index += 2)
	{
		Units.push_back(static_cast<uint16>(Bytes[Index] | Bytes[Index + 1] << 8));
	}
	for (const std::size_t Count : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{4}, std::size_t{5}, Units.size()})
	{
		CHECK(Crc32OfUnits(std::span(Units).first(Count)) == Crc32(std::span(Bytes).first(2 * Count)));
	}
}
