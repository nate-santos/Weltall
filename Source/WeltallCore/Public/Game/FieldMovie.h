#pragma once

#include "Field/FieldMovieCues.h"
#include "Field/FieldRequests.h"
#include "Foundation/Result.h"
#include "Media/MoviePlayer.h"
#include "Sound/SoundFiles.h"

#include <memory>
#include <optional>
#include <span>

namespace Weltall
{
	class FGameSession;

	/**
	 * A movie the field plays inside itself (FE 60, FE 67, FE A0) beside the field's frames: its player
	 * (from its start sector and frame to its end frame, or rewinding; its own sound unless the field
	 * silences it), and the cue set's sound effects, played as the movie's frames pass.
	 */
	class FFieldMovie
	{
	public:
		/**
		 * The movie the field's request asks for: its player started (its first picture read, shown once
		 * the field's first frame of the movie is over) and the cue set's effect file loaded. The session
		 * must outlive it.
		 */
		WELTALLCORE_API static TResult<std::unique_ptr<FFieldMovie>> Start(FGameSession& Session, const FFieldRequest& Request);

		/** Stops the movie and gives up its cues' effects. */
		WELTALLCORE_API ~FFieldMovie();
		FFieldMovie(const FFieldMovie&) = delete;
		FFieldMovie& operator=(const FFieldMovie&) = delete;

		/**
		 * The vertical blanks of the field's frame pass: the pictures due shown, the cues due played.
		 * Returns the frame on screen after them.
		 */
		[[nodiscard]] WELTALLCORE_API TResult<int32> Advance(int32 Vblanks);

		/** The field skipped the movie: its sound fades out and no picture or cue follows. */
		WELTALLCORE_API void Skip();

		const FMoviePlayer& Player() const { return MoviePlayer; }
		const FFieldMovieRequest& Request() const { return Movie; }
		EFieldMovieView View() const { return MovieView; }

	private:
		FFieldMovie(FGameSession& InSession, const FFieldRequest& Request, const FCdImage& Image, FMovieLocation Location,
			std::optional<FMovieSubtitles> Subtitles);

		std::optional<FError> LoadCues();
		void PlayCues();

		FGameSession& Session;
		FFieldMovieRequest Movie;
		EFieldMovieView MovieView;
		FMoviePlayer MoviePlayer;
		std::span<const FFieldMovieCue> Cues;
		std::size_t NextCue = 0;
		std::optional<FSoundEffectFile> CueEffects;
		bool bCueEffectsRegistered = false;
		bool bSkipped = false;
	};
}
