#include "Text/MessageBlocks.h"

#include <catch_amalgamated.hpp>

#include <array>

using namespace Weltall;

TEST_CASE("A file of messages reads back the blocks written to it", "[Text]")
{
	const std::array<std::string_view, 6> Texts = {"", "\n", "Fei\n\"Hey, Alice!\"", "Closing.\n", "    Yes, please!\n\n    No", "message x/1\n{\n;"};
	std::string File;
	for (std::size_t Text = 0; Text < Texts.size(); ++Text)
	{
		AppendMessageBlock(File, "field/4/" + std::to_string(Text), Texts[Text], Text % 2 == 0 ? "48 x 3" : "");
	}
	std::vector<FMessageFileProblem> Problems;
	const std::vector<FMessageBlock> Blocks = ParseMessageBlocks(File, Problems);
	CHECK(Problems.empty());
	REQUIRE(Blocks.size() == Texts.size());
	for (std::size_t Text = 0; Text < Texts.size(); ++Text)
	{
		CHECK(Blocks[Text].Id == "field/4/" + std::to_string(Text));
		CHECK(Blocks[Text].Text == Texts[Text]);
	}
	CHECK(Blocks[0].Line == 1);
	CHECK(Blocks[1].Line == 4);
	CHECK(File.starts_with("message field/4/0  ; 48 x 3\n{\n}\nmessage field/4/1\n{\n\n\n}\n"));
}

TEST_CASE("A file of messages takes Windows' line ends, a byte order mark and comments", "[Text]")
{
	const std::string_view File = "\xEF\xBB\xBF; the Menu\r\n\r\n  message menu/0  ; 16 x 1\r\n\r\n{\r\nNew Game \r\n }\r\n";
	std::vector<FMessageFileProblem> Problems;
	const std::vector<FMessageBlock> Blocks = ParseMessageBlocks(File, Problems);
	CHECK(Problems.empty());
	REQUIRE(Blocks.size() == 1);
	CHECK(Blocks[0].Id == "menu/0");
	CHECK(Blocks[0].Text == "New Game ");
	CHECK(Blocks[0].Line == 3);
}

TEST_CASE("What is no part of a message is reported and the reading goes on", "[Text]")
{
	const std::string_view File = "stray words\nmessage a/1\nno brace\nmessage b/2 extra\nmessage b/2\n{\nkept\n}\nmessage c/3\n{\nunended";
	std::vector<FMessageFileProblem> Problems;
	const std::vector<FMessageBlock> Blocks = ParseMessageBlocks(File, Problems);
	REQUIRE(Blocks.size() == 1);
	CHECK(Blocks[0].Id == "b/2");
	CHECK(Blocks[0].Text == "kept");
	REQUIRE(Problems.size() == 5);
	CHECK(Problems[0].Line == 1);
	CHECK(Problems[1].Line == 2);
	CHECK(Problems[1].Message.find("has no text") != std::string::npos);
	CHECK(Problems[2].Line == 3);
	CHECK(Problems[3].Line == 4);
	CHECK(Problems[4].Line == 9);
	CHECK(Problems[4].Message.find("never ends") != std::string::npos);
}
