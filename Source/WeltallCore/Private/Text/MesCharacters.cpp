#include "Text/MesCharacters.h"

#include "Text/MesGlyphs.h"

#include <array>

namespace Weltall
{
	namespace
	{
		constexpr uint8 SecondPage = 0xFE;
		/** The full-width symbols from 61h to 92h; empty for the buttons (marks of their own) and the two kept twice. */
		constexpr std::array<std::string_view, 0x32> Symbols = {
			"０", "１", "２", "３", "４", "５", "６", "７", "８", "９", "％", "＆", "☆", "", "", "", "※", "→", "←", "↑", "↓", "．", "：", "‐",
			"／", "‥", "“", "」", "＋", "－", "", "＜", "＞", "", "", "『", "』", "＃", "、", "。", "・", "？", "！", "＿", "～", "…", "”", "（",
			"）", "「",
		};
		constexpr uint8 FirstSymbol = 0x61;
		/** Hiragana from ぁ (U+3041) to ん, codes 93h to E5h. */
		constexpr uint8 FirstHiragana = 0x93;
		constexpr uint32 HiraganaStart = 0x3041;
		constexpr uint32 HiraganaCount = 0x53;
		/** Katakana from ァ (U+30A1) to ワ, then ヲ ン ヴ ヵ ヶ ー (the font lacks ヰ and ヱ): codes E6h to FDh, then FE 00 on. */
		constexpr uint8 FirstKatakana = 0xE6;
		constexpr uint32 KatakanaStart = 0x30A1;
		constexpr uint32 KatakanaRun = 0x30EF - KatakanaStart + 1;
		constexpr std::array<uint32, 6> KatakanaAfter = {0x30F2, 0x30F3, 0x30F4, 0x30F5, 0x30F6, 0x30FC};
		constexpr uint32 FirstPageKatakana = 0xFE - FirstKatakana;

		std::string Utf8(uint32 Point)
		{
			std::string Text;
			if (Point < 0x80)
			{
				Text.push_back(static_cast<char>(Point));
			}
			else if (Point < 0x800)
			{
				Text.push_back(static_cast<char>(0xC0 | Point >> 6));
				Text.push_back(static_cast<char>(0x80 | (Point & 0x3F)));
			}
			else
			{
				Text.push_back(static_cast<char>(0xE0 | Point >> 12));
				Text.push_back(static_cast<char>(0x80 | (Point >> 6 & 0x3F)));
				Text.push_back(static_cast<char>(0x80 | (Point & 0x3F)));
			}
			return Text;
		}

		FMesCharacters MakePlain()
		{
			FMesCharacters Characters;
			for (int32 Character = 0x20; Character < 0x7F; ++Character)
			{
				if (const std::optional<FMesCode> Code = MesCodeOfCharacter(static_cast<char>(Character)))
				{
					Characters.Set(*Code, std::string(1, static_cast<char>(Character)));
				}
			}
			return Characters;
		}

		FMesCharacters MakeFont()
		{
			FMesCharacters Characters = MakePlain();
			for (std::size_t Symbol = 0; Symbol < Symbols.size(); ++Symbol)
			{
				if (!Symbols[Symbol].empty())
				{
					Characters.Set({0, static_cast<uint8>(FirstSymbol + Symbol)}, Symbols[Symbol]);
				}
			}
			for (uint32 Kana = 0; Kana < HiraganaCount; ++Kana)
			{
				Characters.Set({0, static_cast<uint8>(FirstHiragana + Kana)}, Utf8(HiraganaStart + Kana));
			}
			for (uint32 Kana = 0; Kana < KatakanaRun + KatakanaAfter.size(); ++Kana)
			{
				const uint32 Point = Kana < KatakanaRun ? KatakanaStart + Kana : KatakanaAfter[Kana - KatakanaRun];
				const FMesCode Code = Kana < FirstPageKatakana ? FMesCode{0, static_cast<uint8>(FirstKatakana + Kana)}
					: FMesCode{SecondPage, static_cast<uint8>(Kana - FirstPageKatakana)};
				Characters.Set(Code, Utf8(Point));
			}
			return Characters;
		}
	}

	std::size_t Utf8SequenceLength(std::string_view Text)
	{
		if (Text.empty())
		{
			return 0;
		}
		const uint8 Lead = static_cast<uint8>(Text.front());
		const std::size_t Length = Lead < 0x80 ? 1 : (Lead & 0xE0) == 0xC0 ? 2 : (Lead & 0xF0) == 0xE0 ? 3 : (Lead & 0xF8) == 0xF0 ? 4 : 0;
		if (Length == 0 || Length > Text.size())
		{
			return 0;
		}
		for (std::size_t Byte = 1; Byte < Length; ++Byte)
		{
			if ((static_cast<uint8>(Text[Byte]) & 0xC0) != 0x80)
			{
				return 0;
			}
		}
		return Length;
	}

	const FMesCharacters& FMesCharacters::Plain()
	{
		static const FMesCharacters Characters = MakePlain();
		return Characters;
	}

	const FMesCharacters& FMesCharacters::Font()
	{
		static const FMesCharacters Characters = MakeFont();
		return Characters;
	}

	void FMesCharacters::Set(FMesCode Code, std::string_view Character)
	{
		const uint16 Wanted = Key(Code);
		if (const auto Old = ByCode.find(Wanted); Old != ByCode.end())
		{
			ByCharacter.erase(Old->second);
			ByCode.erase(Old);
		}
		if (const auto Old = ByCharacter.find(std::string(Character)); Old != ByCharacter.end())
		{
			ByCode.erase(Old->second);
			ByCharacter.erase(Old);
		}
		ByCode.emplace(Wanted, std::string(Character));
		ByCharacter.emplace(std::string(Character), Wanted);
	}

	std::string_view FMesCharacters::CharacterOf(FMesCode Code) const
	{
		const auto Found = ByCode.find(Key(Code));
		return Found != ByCode.end() ? std::string_view(Found->second) : std::string_view();
	}

	std::optional<FMesCharacterCode> FMesCharacters::CodeAt(std::string_view Text) const
	{
		const std::size_t Length = Utf8SequenceLength(Text);
		if (Length == 0)
		{
			return std::nullopt;
		}
		const auto Found = ByCharacter.find(std::string(Text.substr(0, Length)));
		if (Found == ByCharacter.end())
		{
			return std::nullopt;
		}
		return FMesCharacterCode{{static_cast<uint8>(Found->second >> 8), static_cast<uint8>(Found->second)}, Length};
	}
}
