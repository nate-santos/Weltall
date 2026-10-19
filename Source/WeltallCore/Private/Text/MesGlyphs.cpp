#include "Text/MesGlyphs.h"

#include <array>
#include <string_view>

namespace Weltall
{
	namespace
	{
		constexpr uint8 Space = 0x10;
		constexpr uint8 Digits = 0x16;
		constexpr uint8 Capitals = 0x20;
		constexpr uint8 Smalls = 0x3D;
		/** The marks from code 11h, and those from 57h. */
		constexpr std::string_view FirstMarks = "+,-./";
		constexpr uint8 FirstMark = 0x11;
		constexpr std::string_view LaterMarks = "!\"#%&'():?";
		constexpr uint8 LaterMark = 0x57;
		/** The brackets and the equals sign between the capitals and the small letters. */
		constexpr std::string_view Brackets = "[]=";
		constexpr uint8 FirstBracket = 0x3A;

		/** The full-width Shift-JIS forms of the space and of the marks the font has narrow glyphs for. */
		struct FFullWidthMark
		{
			char Plain;
			uint16 FullWidth;
		};
		constexpr std::array<FFullWidthMark, 19> FullWidthMarks = {{
			{' ', 0x8140}, {',', 0x8143}, {'.', 0x8144}, {':', 0x8146}, {'?', 0x8148}, {'!', 0x8149}, {'/', 0x815E}, {'\'', 0x8166},
			{'"', 0x8168}, {'(', 0x8169}, {')', 0x816A}, {'[', 0x816D}, {']', 0x816E}, {'+', 0x817B}, {'-', 0x817C}, {'=', 0x8181},
			{'%', 0x8193}, {'#', 0x8194}, {'&', 0x8195},
		}};
		constexpr uint16 FullWidthDigits = 0x824F;
		constexpr uint16 FullWidthCapitals = 0x8260;
		constexpr uint16 FullWidthSmalls = 0x8281;
	}

	std::optional<FMesCode> MesCodeOfCharacter(char Character)
	{
		if (Character == ' ')
		{
			return FMesCode{0, Space};
		}
		if (Character >= '0' && Character <= '9')
		{
			return FMesCode{0, static_cast<uint8>(Digits + Character - '0')};
		}
		if (Character >= 'A' && Character <= 'Z')
		{
			return FMesCode{0, static_cast<uint8>(Capitals + Character - 'A')};
		}
		if (Character >= 'a' && Character <= 'z')
		{
			return FMesCode{0, static_cast<uint8>(Smalls + Character - 'a')};
		}
		if (const std::size_t Mark = FirstMarks.find(Character); Mark != std::string_view::npos)
		{
			return FMesCode{0, static_cast<uint8>(FirstMark + Mark)};
		}
		if (const std::size_t Mark = LaterMarks.find(Character); Mark != std::string_view::npos)
		{
			return FMesCode{0, static_cast<uint8>(LaterMark + Mark)};
		}
		if (const std::size_t Mark = Brackets.find(Character); Mark != std::string_view::npos)
		{
			return FMesCode{0, static_cast<uint8>(FirstBracket + Mark)};
		}
		return std::nullopt;
	}

	std::optional<char> PlainOfFullWidth(uint8 Lead, uint8 Second)
	{
		const uint16 Character = static_cast<uint16>(Lead << 8 | Second);
		if (Character >= FullWidthDigits && Character < FullWidthDigits + 10)
		{
			return static_cast<char>('0' + Character - FullWidthDigits);
		}
		if (Character >= FullWidthCapitals && Character < FullWidthCapitals + 26)
		{
			return static_cast<char>('A' + Character - FullWidthCapitals);
		}
		if (Character >= FullWidthSmalls && Character < FullWidthSmalls + 26)
		{
			return static_cast<char>('a' + Character - FullWidthSmalls);
		}
		for (const FFullWidthMark& Mark : FullWidthMarks)
		{
			if (Mark.FullWidth == Character)
			{
				return Mark.Plain;
			}
		}
		return std::nullopt;
	}

	std::optional<std::array<uint8, 2>> FullWidthOfPlain(char Plain)
	{
		uint16 Character = 0;
		if (Plain >= '0' && Plain <= '9')
		{
			Character = static_cast<uint16>(FullWidthDigits + Plain - '0');
		}
		else if (Plain >= 'A' && Plain <= 'Z')
		{
			Character = static_cast<uint16>(FullWidthCapitals + Plain - 'A');
		}
		else if (Plain >= 'a' && Plain <= 'z')
		{
			Character = static_cast<uint16>(FullWidthSmalls + Plain - 'a');
		}
		for (const FFullWidthMark& Mark : FullWidthMarks)
		{
			Character = Mark.Plain == Plain ? Mark.FullWidth : Character;
		}
		if (Character == 0)
		{
			return std::nullopt;
		}
		return std::array<uint8, 2>{static_cast<uint8>(Character >> 8), static_cast<uint8>(Character)};
	}

	std::optional<FMesCode> MesCodeOfFullWidth(uint8 Lead, uint8 Second)
	{
		const std::optional<char> Plain = PlainOfFullWidth(Lead, Second);
		return Plain ? MesCodeOfCharacter(*Plain) : std::nullopt;
	}
}
