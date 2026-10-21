#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"

#include <memory>
#include <span>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;
	class FPsxExecutable;

	/**
	 * One line of the supported undub's movie subtitles: a picture over the movie's own, a whole number of the
	 * movie's 16-pixel columns wide, shown on the frames from FirstFrame up to EndFrame.
	 */
	struct FMovieSubtitleLine
	{
		uint32 FirstFrame = 0;
		uint32 EndFrame = 0;
		/** Where the picture lies in the movie's, in its pixels. */
		int32 X = 0;
		int32 Y = 0;
		int32 Width = 0;
		int32 Height = 0;
		/** Where its pixels start in the movie's subtitle file: each column's Height rows of 16 in turn, 4 bits a pixel, low bits first. */
		uint32 Offset = 0;

		bool operator==(const FMovieSubtitleLine&) const = default;
	};

	/** A subtitled movie: the index record of the file its pictures are in, and its lines in the order they show. */
	struct FMovieSubtitleEntry
	{
		int32 Movie = 0;
		std::size_t Record = 0;
		std::vector<FMovieSubtitleLine> Lines;
	};

	/**
	 * The movie subtitles the supported undub keeps in its executable past the file index's records: the movies
	 * it subtitles and their lines. Its patch finds a movie's entry as the movie player asks for the movie's file,
	 * and draws the lines into the pictures the player decodes.
	 */
	class FMovieSubtitleTable
	{
	public:
		/**
		 * The table in the executable, which the caller has identified as one of the supported undub's, or as one with
		 * its subtitle code (HasUndubCode).
		 */
		WELTALLCORE_API static FMovieSubtitleTable Read(const FPsxExecutable& Executable);

		/**
		 * The executable carries the supported undub's subtitle code, which reads its table as Read does: as Perfect
		 * Works Build's executables do, with a table of their own.
		 */
		WELTALLCORE_API static bool HasUndubCode(const FPsxExecutable& Executable);

		/** Movie Number's subtitles, or null for a movie without them. */
		WELTALLCORE_API const FMovieSubtitleEntry* Find(int32 Movie) const;

		const std::vector<FMovieSubtitleEntry>& Entries() const { return Movies; }

	private:
		std::vector<FMovieSubtitleEntry> Movies;
	};

	/** A line's picture as it is blended over the movie's: R, G, B, A a pixel, the colour already multiplied by the alpha (255 covers the movie). */
	struct FMovieSubtitlePicture
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Width = 0;
		int32 Height = 0;
		std::vector<uint8> Pixels;
	};

	/**
	 * A movie's subtitles ready to show: its lines' pictures, and the line the undub draws over each picture the
	 * player decodes. Lines only go forward, as the patch's do, until the movie's stream starts again.
	 */
	class FMovieSubtitles
	{
	public:
		/** Reads the entry's file of pictures from the disc whose index the table came from. */
		WELTALLCORE_API static TResult<FMovieSubtitles> Load(const FCdImage& Image, const FGameFileIndex& Index, const FMovieSubtitleEntry& Entry);

		/**
		 * The lines' pictures from their file: 16 colours of blue, green, red and an alpha out of 128 (128 covers
		 * the movie), then each line's pixels.
		 */
		WELTALLCORE_API static TResult<FMovieSubtitles> FromFile(std::span<const uint8> File, std::span<const FMovieSubtitleLine> Lines);

		/** The movie's stream starts again: lines are looked for from the first. */
		void Restart() { NextLine = 0; }

		/**
		 * The picture drawn over the movie's picture of Frame, or null: the first line not yet over, from its first
		 * frame on. A line Frame has reached the end of is passed for good.
		 */
		WELTALLCORE_API std::shared_ptr<const FMovieSubtitlePicture> At(uint32 Frame);

	private:
		std::vector<FMovieSubtitleLine> Lines;
		std::vector<std::shared_ptr<const FMovieSubtitlePicture>> Pictures;
		std::size_t NextLine = 0;
	};
}
