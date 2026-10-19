#include "Foundation/Crc32.h"

#include <array>

namespace Weltall
{
	namespace
	{
		constexpr uint32 Polynomial = 0xEDB88320u;
		constexpr std::size_t Slices = 8;

		/** Table s gives a byte's remainder once s more zero bytes have followed it, so eight bytes are folded at a time. */
		using FTables = std::array<std::array<uint32, 256>, Slices>;

		constexpr FTables MakeTables()
		{
			FTables Tables{};
			for (uint32 Byte = 0; Byte < 256; ++Byte)
			{
				uint32 Remainder = Byte;
				for (int32 Bit = 0; Bit < 8; ++Bit)
				{
					Remainder = Remainder & 1 ? Remainder >> 1 ^ Polynomial : Remainder >> 1;
				}
				Tables[0][Byte] = Remainder;
			}
			for (std::size_t Slice = 1; Slice < Slices; ++Slice)
			{
				for (uint32 Byte = 0; Byte < 256; ++Byte)
				{
					const uint32 Previous = Tables[Slice - 1][Byte];
					Tables[Slice][Byte] = Previous >> 8 ^ Tables[0][Previous & 0xFF];
				}
			}
			return Tables;
		}

		constexpr FTables Tables = MakeTables();

		uint32 FoldByte(uint32 Crc, uint8 Byte)
		{
			return Crc >> 8 ^ Tables[0][(Crc ^ Byte) & 0xFF];
		}

		/** Folds eight bytes, the first four as Low (first byte lowest) and the next four as High. */
		uint32 FoldEight(uint32 Crc, uint32 Low, uint32 High)
		{
			Low ^= Crc;
			return Tables[7][Low & 0xFF] ^ Tables[6][Low >> 8 & 0xFF] ^ Tables[5][Low >> 16 & 0xFF] ^ Tables[4][Low >> 24] ^ Tables[3][High & 0xFF]
				^ Tables[2][High >> 8 & 0xFF] ^ Tables[1][High >> 16 & 0xFF] ^ Tables[0][High >> 24];
		}
	}

	uint32 Crc32(std::span<const uint8> Bytes, uint32 Crc)
	{
		Crc = ~Crc;
		std::size_t At = 0;
		for (; At + Slices <= Bytes.size(); At += Slices)
		{
			const uint8* Eight = &Bytes[At];
			Crc = FoldEight(Crc, uint32{Eight[0]} | uint32{Eight[1]} << 8 | uint32{Eight[2]} << 16 | uint32{Eight[3]} << 24,
				uint32{Eight[4]} | uint32{Eight[5]} << 8 | uint32{Eight[6]} << 16 | uint32{Eight[7]} << 24);
		}
		for (; At < Bytes.size(); ++At)
		{
			Crc = FoldByte(Crc, Bytes[At]);
		}
		return ~Crc;
	}

	uint32 Crc32OfUnits(std::span<const uint16> Units, uint32 Crc)
	{
		Crc = ~Crc;
		std::size_t At = 0;
		for (; At + 4 <= Units.size(); At += 4)
		{
			Crc = FoldEight(Crc, uint32{Units[At]} | uint32{Units[At + 1]} << 16, uint32{Units[At + 2]} | uint32{Units[At + 3]} << 16);
		}
		for (; At < Units.size(); ++At)
		{
			Crc = FoldByte(FoldByte(Crc, static_cast<uint8>(Units[At])), static_cast<uint8>(Units[At] >> 8));
		}
		return ~Crc;
	}
}
