#include "Text/MessageBlocks.h"

#include <optional>

namespace Weltall
{
	namespace
	{
		constexpr std::string_view Keyword = "message";
		constexpr std::string_view ByteOrderMark = "\xEF\xBB\xBF";
		constexpr std::string_view Blanks = " \t";

		std::string_view Trimmed(std::string_view Line)
		{
			const std::size_t First = Line.find_first_not_of(Blanks);
			if (First == std::string_view::npos)
			{
				return {};
			}
			return Line.substr(First, Line.find_last_not_of(Blanks) - First + 1);
		}

		/** The next line of Rest without its line break and a carriage return before it; Rest moves past it. */
		std::string_view NextLine(std::string_view& Rest)
		{
			const std::size_t Break = Rest.find('\n');
			std::string_view Line = Rest.substr(0, Break);
			Rest.remove_prefix(Break == std::string_view::npos ? Rest.size() : Break + 1);
			if (!Line.empty() && Line.back() == '\r')
			{
				Line.remove_suffix(1);
			}
			return Line;
		}

		/** The id a header names, or none where the line is no header. */
		std::optional<std::string_view> HeaderId(std::string_view Line)
		{
			if (!Line.starts_with(Keyword) || Line.size() == Keyword.size() || Blanks.find(Line[Keyword.size()]) == std::string_view::npos)
			{
				return std::nullopt;
			}
			const std::string_view Rest = Trimmed(Line.substr(Keyword.size()));
			const std::string_view Id = Rest.substr(0, Rest.find_first_of(" \t;"));
			const std::string_view After = Trimmed(Rest.substr(Id.size()));
			if (Id.empty() || (!After.empty() && After.front() != ';'))
			{
				return std::nullopt;
			}
			return Id;
		}
	}

	void AppendMessageBlock(std::string& File, std::string_view Id, std::string_view Text, std::string_view Comment)
	{
		File += Keyword;
		File += ' ';
		File += Id;
		if (!Comment.empty())
		{
			File += "  ; ";
			File += Comment;
		}
		File += "\n{\n";
		if (!Text.empty())
		{
			File += Text;
			File += '\n';
		}
		File += "}\n";
	}

	std::vector<FMessageBlock> ParseMessageBlocks(std::string_view File, std::vector<FMessageFileProblem>& Problems)
	{
		if (File.starts_with(ByteOrderMark))
		{
			File.remove_prefix(ByteOrderMark.size());
		}
		enum class EState : uint8 { Outside, Header, Text };
		std::vector<FMessageBlock> Blocks;
		EState State = EState::Outside;
		FMessageBlock Block;
		std::vector<std::string_view> Lines;
		int32 Number = 0;
		while (!File.empty())
		{
			const std::string_view Line = NextLine(File);
			++Number;
			const std::string_view Words = Trimmed(Line);
			if (State == EState::Text)
			{
				if (Words != "}")
				{
					Lines.push_back(Line);
					continue;
				}
				for (std::size_t Each = 0; Each < Lines.size(); ++Each)
				{
					Block.Text += Each == 0 ? "" : "\n";
					Block.Text += Lines[Each];
				}
				Blocks.push_back(std::move(Block));
				State = EState::Outside;
				continue;
			}
			if (State == EState::Header)
			{
				if (Words.empty())
				{
					continue;
				}
				if (Words == "{")
				{
					State = EState::Text;
					Lines.clear();
					continue;
				}
				Problems.push_back({Block.Line, "the message " + Block.Id + " has no text: a brace on its own line begins it"});
				State = EState::Outside;
			}
			if (Words.empty() || Words.front() == ';')
			{
				continue;
			}
			if (const std::optional<std::string_view> Id = HeaderId(Words))
			{
				Block = FMessageBlock{std::string(*Id), {}, Number};
				State = EState::Header;
				continue;
			}
			Problems.push_back({Number, "a line outside the messages: \"" + std::string(Words) + "\""});
		}
		if (State != EState::Outside)
		{
			Problems.push_back({Block.Line, "the message " + Block.Id + " never ends: a brace on its own line ends it"});
		}
		return Blocks;
	}
}
