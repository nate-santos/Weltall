#pragma once

#include "Foundation/Result.h"
#include "Graphics/Mdec.h"
#include "Input/PadState.h"
#include "Media/GameMovies.h"
#include "Media/MovieStream.h"
#include "Media/MovieSubtitles.h"

#include <deque>
#include <memory>
#include <optional>

namespace Weltall
{
	class FGameSoundSystem;

	/** How a movie is played: the part of it, what happens at its end, its sound, and who may skip it. */
	struct FMoviePlayback
	{
		/** The end frame the Movie program plays to unless it is asked for another. */
		static constexpr uint32 DefaultEndFrame = 233;

		/** Where reading starts, in the movie's sectors, and the first frame shown: the frames read before it are passed over. */
		uint32 StartSector = 0;
		uint32 StartFrame = 1;
		/** The frame the movie stops at, shown; a rewinding movie starts again from its start instead, until it is stopped. */
		uint32 EndFrame = DefaultEndFrame;
		bool bRewinds = false;
		/** Whether the drive plays the movie's sound: without it the movie is silent. */
		bool bSound = true;
		/** Cross or Start skips it, as the Movie program reads the pad (a held one only once its auto-repeat fires). */
		bool bSkippable = false;
	};

	/**
	 * A movie as the game's movie player plays it, a vertical blank at a time: its pictures at 15 a
	 * second, the pace the drive delivers them at (picture n shows from the first vertical blank at or
	 * past (n - 1) / 15 seconds), until it shows its end frame (or runs out). Its sound goes from the
	 * same moment into the SPU's CD input, as the drive's does, at the CD volume the player sets:
	 * silent at first, faded in once a picture past the start frame shows and out from three pictures
	 * before the end frame. A skip fades the sound out over the 5 vertical blanks before the movie
	 * stops. Either way it stops on the picture it showed last, and its sound stops with it.
	 */
	class FMoviePlayer
	{
	public:
		/** The vertical blanks a skip's fade lasts before the movie stops. */
		static constexpr int32 SkipVblanks = 5;

		/**
		 * The movie at Location on the image, played into the game's sound, with the undub's subtitles drawn over
		 * it when given; the image and the sound must outlive the player.
		 */
		WELTALLCORE_API FMoviePlayer(const FCdImage& Image, FMovieLocation Location, const FMoviePlayback& InPlayback, FGameSoundSystem& InSound,
			std::optional<FMovieSubtitles> InSubtitles = std::nullopt);
		WELTALLCORE_API ~FMoviePlayer();
		FMoviePlayer(const FMoviePlayer&) = delete;
		FMoviePlayer& operator=(const FMoviePlayer&) = delete;

		/**
		 * Reads the first picture and the sound ahead of it, which show and start together: the movie's
		 * first moment. Returns whether it plays; a read or decode error ends it.
		 */
		WELTALLCORE_API TResult<bool> Start();

		/**
		 * One vertical blank with the buttons the player holds: the skip read when the movie allows it,
		 * the picture due shown. Returns whether the movie still plays; a read or decode error ends it.
		 */
		WELTALLCORE_API TResult<bool> Vblank(FPadState Held);

		/** The program skips the movie: its sound fades out, no picture follows, and it stops after SkipVblanks more vertical blanks. */
		WELTALLCORE_API void Skip();

		/** Stops the movie at once, on the picture it shows. */
		WELTALLCORE_API void Stop();

		/** The picture on screen now: null before the first; shared, so a frame being drawn keeps it. */
		std::shared_ptr<const FMdecPicture> Picture() const { return Shown; }

		/** The subtitle drawn over the picture on screen, or null. */
		std::shared_ptr<const FMovieSubtitlePicture> Subtitle() const { return ShownSubtitle; }

		/** How many pictures have been shown; 0 before the first. */
		uint32 PictureNumber() const { return PicturesShown; }

		/** The frame number the movie's data gives the picture on screen; 0 before the first. */
		uint32 FrameNumber() const { return ShownFrame; }

		bool IsFinished() const { return bFinished; }

	private:
		/** Reads on until the next picture is decoded, queueing the sound met on the way. */
		TResult<bool> DecodeNext();
		TResult<bool> ShowDue();
		TResult<bool> ReadAhead();
		void PollSkip(FPadState Buttons);
		/** The CD volume the player sets as a picture of the frame shows. */
		void FadeFor(uint32 Frame);
		/** Reading starts again from the start: the drive's sound so far is cut. */
		void Rewind();
		/** The drive stops: its sound is cut. */
		void StopSound();
		void Finish();

		const FCdImage& Image;
		FMovieLocation Location;
		FMoviePlayback Playback;
		FGameSoundSystem& Sound;
		std::optional<FMovieSubtitles> Subtitles;
		std::optional<FMovieStream> Stream;
		std::vector<int16> SoundScratch;
		struct FDecodedPicture
		{
			std::shared_ptr<const FMdecPicture> Picture;
			uint32 FrameNumber = 0;
			std::shared_ptr<const FMovieSubtitlePicture> Subtitle;
		};

		std::deque<FDecodedPicture> Decoded;
		std::shared_ptr<const FMdecPicture> Shown;
		std::shared_ptr<const FMovieSubtitlePicture> ShownSubtitle;
		uint32 ShownFrame = 0;
		uint32 PicturesShown = 0;
		/** The vertical blanks since the movie's first picture. */
		uint64 Vblanks = 0;
		/** The last reading; before the first, every button, so one held from the start is no press. */
		uint32 Polled = ~0u;
		int32 RepeatCount = 0;
		int32 RepeatDelay = 0;
		/** After a skip, the vertical blanks until the movie stops; 0 while it plays on. */
		int32 StopVblanks = 0;
		/** Skipped by its program: no picture follows while the sound fades. */
		bool bPicturesStopped = false;
		bool bStarted = false;
		bool bFadedIn = false;
		bool bFadingOut = false;
		bool bStreamEnded = false;
		bool bFinished = false;
	};
}
