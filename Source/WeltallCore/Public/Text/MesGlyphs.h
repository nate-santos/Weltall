#pragma once

#include "Foundation/Types.h"
#include "Text/MesFont.h"

#include <array>
#include <optional>
#include <string_view>

namespace Weltall
{
	/** The message font's code for a character of plain text: its letters, digits, space and marks; none for any other. */
	WELTALLCORE_API std::optional<FMesCode> MesCodeOfCharacter(char Character);

	/**
	 * The plain character of a full-width Shift-JIS one of a save's title or a place's name: a letter, a digit,
	 * a space or one of the marks those names use; none for any other.
	 */
	WELTALLCORE_API std::optional<char> PlainOfFullWidth(uint8 Lead, uint8 Second);

	/** The message font's code for such a full-width character. */
	WELTALLCORE_API std::optional<FMesCode> MesCodeOfFullWidth(uint8 Lead, uint8 Second);

	/** The full-width Shift-JIS character (lead, second byte) of a plain one PlainOfFullWidth gives; none for any other. */
	WELTALLCORE_API std::optional<std::array<uint8, 2>> FullWidthOfPlain(char Plain);
}
