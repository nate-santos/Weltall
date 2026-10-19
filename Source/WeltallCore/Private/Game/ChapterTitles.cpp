#include "Game/ChapterTitles.h"

#include "Media/CdImage.h"
#include "Media/GameFileIndex.h"
#include "Text/MesGlyphs.h"
#include "Text/MessageOverrides.h"
#include "Text/MessageTableIds.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr int32 TitlesDirectory = 17;
		constexpr int32 TitlesFile = 1;

		/** A line's title as plain text, the spaces that fill it out to the line's width cut. */
		std::string LineTitle(std::span<const uint8> Line)
		{
			std::string Title;
			for (std::size_t At = 0; At + 1 < Line.size() && Line[At] > 0x7F; At += 2)
			{
				Title.push_back(PlainOfFullWidth(Line[At], Line[At + 1]).value_or(' '));
			}
			Title.erase(Title.find_last_not_of(' ') + 1);
			return Title;
		}

		/** Visit(At, End) for each line of the file: a line ends at its line feed, a two-byte character passed whole. */
		template <typename FVisit>
		void ForEachLine(std::span<const uint8> Lines, FVisit&& Visit)
		{
			std::size_t At = 0;
			while (At < Lines.size())
			{
				std::size_t End = At;
				while (End < Lines.size() && Lines[End] != '\n')
				{
					End += Lines[End] > 0x7F ? 2 : 1;
				}
				End = std::min(End, Lines.size());
				Visit(At, End);
				At = End + 1;
			}
		}

		/** Title written over the line's full-width characters, the rest of them full-width spaces; false where it cannot be. */
		bool WriteTitle(std::span<uint8> Line, std::string_view Title)
		{
			std::size_t Width = 0;
			while (Width + 1 < Line.size() && Line[Width] > 0x7F)
			{
				Width += 2;
			}
			if (Title.size() * 2 > Width)
			{
				return false;
			}
			std::vector<uint8> Written;
			for (std::size_t Place = 0; Place < Width / 2; ++Place)
			{
				const std::optional<std::array<uint8, 2>> Character = FullWidthOfPlain(Place < Title.size() ? Title[Place] : ' ');
				if (!Character)
				{
					return false;
				}
				Written.insert(Written.end(), Character->begin(), Character->end());
			}
			std::copy(Written.begin(), Written.end(), Line.begin());
			return true;
		}
	}

	TResult<FChapterTitles> FChapterTitles::Read(const FCdImage& Image, const FGameFileIndex& Index)
	{
		const FIndexRecord* Record = Index.Find(TitlesDirectory, TitlesFile);
		if (Record == nullptr)
		{
			return MakeError("the disc lacks the chapters' titles");
		}
		TResult<std::vector<uint8>> Lines = ReadFileBytes(Image, *Record);
		if (!Lines)
		{
			return Lines.Error();
		}
		return Parse(WithPlayersChapterTitles(Image, *Lines));
	}

	FChapterTitles FChapterTitles::Parse(std::span<const uint8> Lines)
	{
		FChapterTitles Titles;
		ForEachLine(Lines, [&Titles, Lines](std::size_t At, std::size_t End)
		{
			std::string Title = LineTitle(Lines.subspan(At, End - At));
			if (!Title.empty())
			{
				if (Titles.List.empty() || Titles.List.back().Title != Title)
				{
					Titles.List.push_back({static_cast<int32>(Titles.List.size()) + 1, Titles.LineCount, std::move(Title)});
				}
				++Titles.LineCount;
			}
		});
		return Titles;
	}

	const FChapter* FChapterTitles::Find(uint16 Progress) const
	{
		const auto After = std::upper_bound(List.begin(), List.end(), Progress, [](uint16 Value, const FChapter& Chapter) { return Value < Chapter.FirstProgress; });
		return After == List.begin() ? nullptr : &*(After - 1);
	}

	std::vector<uint8> WithPlayersChapterTitles(const FCdImage& Image, std::span<const uint8> Lines)
	{
		std::vector<uint8> File(Lines.begin(), Lines.end());
		const std::shared_ptr<const FMessageOverrides> Overrides = Image.MessageOverrides();
		if (!Overrides)
		{
			return File;
		}
		int32 Chapter = 0;
		std::string Previous;
		ForEachLine(Lines, [&](std::size_t At, std::size_t End)
		{
			std::string Title = LineTitle(Lines.subspan(At, End - At));
			if (Title.empty())
			{
				return;
			}
			if (Chapter == 0 || Title != Previous)
			{
				++Chapter;
				Previous = std::move(Title);
			}
			const std::string* Text = Overrides->Text(MessageTables::Chapters, Chapter);
			if (Text != nullptr && !WriteTitle(std::span<uint8>(File).subspan(At, End - At), *Text))
			{
				Overrides->ReportOnce(MessageTables::Chapters, Chapter, "a chapter's title is one line of at most 15 letters, digits, spaces and the marks "
					"+ , - . / ! \" # % & ' ( ) : ? [ ] =: the disc's title is kept");
			}
		});
		for (const int32 Past : Overrides->Messages(MessageTables::Chapters))
		{
			if (Past < 1 || Past > Chapter)
			{
				Overrides->ReportOnce(MessageTables::Chapters, Past, "names no chapter: they are numbered 1 to " + std::to_string(Chapter));
			}
		}
		return File;
	}
}
