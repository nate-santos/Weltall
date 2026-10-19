#pragma once

#include "Foundation/Types.h"
#include "Text/MesFont.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Weltall
{
	/** A glyph's code at the start of a text, and how many of the text's bytes its character takes. */
	struct FMesCharacterCode
	{
		FMesCode Code;
		std::size_t Bytes = 0;
	};

	/**
	 * The characters text is written in, each standing for one glyph of the font: a character is one Unicode code
	 * point, kept as UTF-8. A glyph stands for at most one character and a character for at most one glyph. The
	 * controller's buttons and the box glyph are marks of their own ({Circle}, {box}: MesText.h), never characters.
	 */
	class FMesCharacters
	{
	public:
		/** The English script's own: letters, digits, the space and its marks, each one narrow glyph. */
		WELTALLCORE_API static const FMesCharacters& Plain();

		/**
		 * Every glyph of the USA font that stands for a character: Plain's, then the Japanese font's symbols it keeps
		 * (the full-width digits, …, ～, ※, the arrows) and its kana as their Unicode forms. Two symbols the font
		 * holds twice (％ and ＆) stand for their first glyph only.
		 */
		WELTALLCORE_API static const FMesCharacters& Font();

		/** Code stands for Character from now on, the code's earlier character and the character's earlier code let go. */
		WELTALLCORE_API void Set(FMesCode Code, std::string_view Character);

		/** The character Code stands for; empty for none. */
		WELTALLCORE_API std::string_view CharacterOf(FMesCode Code) const;

		/** The glyph the character at Text's start stands for; none for a character none does, or bytes that are not UTF-8. */
		WELTALLCORE_API std::optional<FMesCharacterCode> CodeAt(std::string_view Text) const;

	private:
		static uint16 Key(FMesCode Code) { return static_cast<uint16>(Code.Lead << 8 | Code.Code); }

		std::unordered_map<uint16, std::string> ByCode;
		std::unordered_map<std::string, uint16> ByCharacter;
	};

	/** The length of the UTF-8 sequence at Text's start; 0 where it is not one. */
	WELTALLCORE_API std::size_t Utf8SequenceLength(std::string_view Text);
}
