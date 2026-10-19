#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <span>
#include <string>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;

	/** A chapter of the story: its number from 1 in the story's order, the first value of the story's progress in it, and its title. */
	struct FChapter
	{
		int32 Number = 0;
		uint16 FirstProgress = 0;
		std::string Title;
	};

	/**
	 * The story's chapters, from the file a save's title is read from: a line for each value of the story's
	 * progress (event variable 0), the lines that follow one another with the same title one chapter.
	 */
	class FChapterTitles
	{
	public:
		/** The disc's file of titles. */
		WELTALLCORE_API static TResult<FChapterTitles> Read(const FCdImage& Image, const FGameFileIndex& Index);

		/** The file's lines, each a title in full-width characters. */
		WELTALLCORE_API static FChapterTitles Parse(std::span<const uint8> Lines);

		std::span<const FChapter> Chapters() const { return List; }

		/** The chapter the story's progress lies in, the last one's past the file's end; null when the file holds none. */
		WELTALLCORE_API const FChapter* Find(uint16 Progress) const;

	private:
		std::vector<FChapter> List;
		/** How many lines the file holds: the progress values that have a line of their own. */
		uint16 LineCount = 0;
	};

	/**
	 * The titles' file (Lines) with the player's chapter titles in place (Assets/text: chapters/<number>, from 1): each
	 * line of a chapter they rename written again in full-width characters, as the disc's are. The chapters' cards and
	 * the saves' places read it. A title longer than its lines, or with a character they cannot hold, keeps the disc's.
	 */
	WELTALLCORE_API std::vector<uint8> WithPlayersChapterTitles(const FCdImage& Image, std::span<const uint8> Lines);
}
