#include "Text/MesCharacters.h"
#include "Text/MesText.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

namespace
{
	using FBytes = std::vector<uint8>;
}

TEST_CASE("The font's characters are its symbols and kana as Unicode", "[Text]")
{
	const FMesCharacters& Font = FMesCharacters::Font();
	CHECK(Font.CharacterOf({0, 0x41}) == "e");
	CHECK(Font.CharacterOf({0, 0x61}) == "０");
	CHECK(Font.CharacterOf({0, 0x71}) == "※");
	CHECK(Font.CharacterOf({0, 0x72}) == "→");
	CHECK(Font.CharacterOf({0, 0x8D}) == "～");
	CHECK(Font.CharacterOf({0, 0x8E}) == "…");
	CHECK(Font.CharacterOf({0, 0x92}) == "「");
	CHECK(Font.CharacterOf({0, 0x93}) == "ぁ");
	CHECK(Font.CharacterOf({0, 0xE5}) == "ん");
	CHECK(Font.CharacterOf({0, 0xE6}) == "ァ");
	CHECK(Font.CharacterOf({0, 0xFD}) == "ジ");
	CHECK(Font.CharacterOf({0xFE, 0x00}) == "ス");
	CHECK(Font.CharacterOf({0xFE, 0x37}) == "ヲ");
	CHECK(Font.CharacterOf({0xFE, 0x3C}) == "ー");
	// The buttons are marks of their own, and the symbols the font holds twice stand for their first glyph.
	CHECK(Font.CharacterOf({0, 0x6E}).empty());
	CHECK(Font.CharacterOf({0, 0x7F}).empty());
	CHECK(Font.CharacterOf({0, 0x82}).empty());
	CHECK(Font.CodeAt("％")->Code == FMesCode{0, 0x6B});
	CHECK(Font.CharacterOf({0xFE, 0x3D}).empty());
}

TEST_CASE("Every character of the font types its own glyph", "[Text]")
{
	const FMesCharacters& Font = FMesCharacters::Font();
	int32 Characters = 0;
	for (int32 Code = 0x10; Code < 0xFE + 0x3D; ++Code)
	{
		const FMesCode Glyph = Code < 0xFE ? FMesCode{0, static_cast<uint8>(Code)} : FMesCode{0xFE, static_cast<uint8>(Code - 0xFE)};
		const std::string_view Character = Font.CharacterOf(Glyph);
		if (Character.empty())
		{
			continue;
		}
		INFO("code " << Code);
		const std::optional<FMesCharacterCode> Typed = Font.CodeAt(Character);
		REQUIRE(Typed);
		CHECK(Typed->Code == Glyph);
		CHECK(Typed->Bytes == Character.size());
		++Characters;
	}
	// 81 narrow glyphs, 50 symbols less the four buttons and the two twice, 83 hiragana, 85 katakana.
	CHECK(Characters == 81 + 44 + 83 + 85);
}

TEST_CASE("The plain characters are the English script's alone", "[Text]")
{
	const FMesCharacters& Plain = FMesCharacters::Plain();
	CHECK(Plain.CodeAt("A")->Code == FMesCode{0, 0x20});
	CHECK(Plain.CodeAt("?")->Code == FMesCode{0, 0x60});
	CHECK(Plain.CharacterOf({0, 0x8E}).empty());
	CHECK_FALSE(Plain.CodeAt("…"));
}

TEST_CASE("A glyph given a new character lets its old one go", "[Text]")
{
	FMesCharacters Characters = FMesCharacters::Font();
	Characters.Set({0, 0x93}, "é");
	CHECK(Characters.CodeAt("é")->Code == FMesCode{0, 0x93});
	CHECK(Characters.CodeAt("é")->Bytes == 2);
	CHECK_FALSE(Characters.CodeAt("ぁ"));
	Characters.Set({0, 0x94}, "é");
	CHECK(Characters.CharacterOf({0, 0x93}).empty());
	CHECK(Characters.CharacterOf({0, 0x94}) == "é");
	// The original font's characters are untouched.
	CHECK(FMesCharacters::Font().CharacterOf({0, 0x93}) == "ぁ");
}

TEST_CASE("Bytes that are not UTF-8 stand for no glyph", "[Text]")
{
	const FMesCharacters& Font = FMesCharacters::Font();
	CHECK_FALSE(Font.CodeAt("\xC3"));
	CHECK_FALSE(Font.CodeAt("\x80" "A"));
	CHECK_FALSE(Font.CodeAt("\xE2\x80"));
	CHECK(Utf8SequenceLength("\xE2\x80\xA6") == 3);
	CHECK(Utf8SequenceLength("\xF0\x9F\x98\x80") == 4);
	CHECK(Utf8SequenceLength("") == 0);
}

TEST_CASE("A message's symbols are written as the font's characters and read back", "[Text]")
{
	// "Wait", "…", a pause, a line break, ※, a kana of the second page, the Circle button.
	const FBytes Codes = {0x36, 0x3D, 0x45, 0x50, 0x8E, 0x0F, 0x00, 0x14, 0x01, 0x71, 0xFE, 0x00, 0x6E, 0x00};
	const std::string Text = MesTextOfCodes(Codes, FMesCharacters::Font());
	CHECK(Text == "Wait…{wait 14}\n※ス{Circle}");
	CHECK(MesCodesOfText(Text, FMesCharacters::Font()) == Codes);
	CHECK(MesTextOfCodes(Codes) == "Wait{8E}{wait 14}\n{71}{FE00}{Circle}");
	CHECK_FALSE(MesCodesOfText("Wait…"));
}

TEST_CASE("A message takes the bytes its text's reading takes", "[Text]")
{
	// A pause of 0 is no end; a box glyph takes two bytes; what follows the 00 is the next message's.
	CHECK(MesMessageSize(FBytes{0x20, 0x0F, 0x00, 0x00, 0x21, 0x00, 0x22}) == 6);
	CHECK(MesMessageSize(FBytes{0xFF, 0xFF, 0x00, 0x20}) == 3);
	CHECK(MesMessageSize(FBytes{0x00, 0x20}) == 1);
	CHECK(MesMessageSize(FBytes{0x20, 0x21}) == 2);
}
