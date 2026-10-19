#include "Game/FieldMovie.h"

#include "Game/GameSession.h"
#include "Sound/GameSoundSession.h"
#include "Sound/SoundDiscFiles.h"

#include <string>

namespace Weltall
{
	namespace
	{
		/** An effect's id: its file's id above its number. */
		constexpr int32 EffectFileShift = 16;

		/** How the player plays the field's movie: the field reads the pad for its skip, and a cue set or the flag silences it. */
		FMoviePlayback PlaybackOf(const FFieldMovieRequest& Movie)
		{
			FMoviePlayback Playback;
			Playback.StartSector = Movie.StartSector;
			Playback.StartFrame = Movie.StartFrame;
			Playback.EndFrame = Movie.EndFrame;
			Playback.bRewinds = Movie.bRewinds;
			Playback.bSound = (Movie.Flags & FFieldMovieRequest::OwnSoundOff) == 0 && Movie.Cues == FFieldMovieRequest::NoCues;
			Playback.bSkippable = false;
			return Playback;
		}
	}

	FFieldMovie::FFieldMovie(FGameSession& InSession, const FFieldRequest& Request, const FCdImage& Image, FMovieLocation Location,
		std::optional<FMovieSubtitles> Subtitles)
		: Session(InSession), Movie(Request.Movie), MovieView(Request.MovieView),
		  MoviePlayer(Image, Location, PlaybackOf(Request.Movie), *InSession.Sound().Sound(), std::move(Subtitles))
	{
	}

	FFieldMovie::~FFieldMovie()
	{
		MoviePlayer.Stop();
		if (bCueEffectsRegistered)
		{
			const FLockedGameSound Locked = Session.Sound().Sound()->Lock();
			Locked->StopAllEffects();
			Locked->UnregisterEffects(*CueEffects);
		}
	}

	TResult<std::unique_ptr<FFieldMovie>> FFieldMovie::Start(FGameSession& Session, const FFieldRequest& Request)
	{
		// The movies are the drive's disc's: each disc has its own.
		const TResult<FGameDisc> Disc = Session.MovieDisc();
		if (!Disc)
		{
			return Disc.Error();
		}
		const std::optional<FMovieLocation> Location = FindMovie(*Disc->Index, Request.Movie.Movie);
		if (!Location)
		{
			return MakeError("the field's movie " + std::to_string(Request.Movie.Movie) + " is not among the disc's movies");
		}
		TResult<std::optional<FMovieSubtitles>> Subtitles = Session.MovieSubtitles(Request.Movie.Movie);
		if (!Subtitles)
		{
			return Subtitles.Error();
		}
		std::unique_ptr<FFieldMovie> Movie(new FFieldMovie(Session, Request, *Disc->Image, *Location, std::move(*Subtitles)));
		if (auto Error = Movie->LoadCues())
		{
			return *Error;
		}
		const TResult<bool> Started = Movie->MoviePlayer.Start();
		if (!Started)
		{
			return Started.Error();
		}
		return Movie;
	}

	std::optional<FError> FFieldMovie::LoadCues()
	{
		if (Movie.Cues == FFieldMovieRequest::NoCues)
		{
			return std::nullopt;
		}
		Cues = Session.FieldMovieCues().Set(Movie.Cues);
		if (Cues.empty())
		{
			return MakeError("the field's movie asks for cue set " + std::to_string(Movie.Cues) + ", which the Field program does not have");
		}
		TResult<FSoundEffectFile> File = ReadSoundFile<FSoundEffectFile>(Session.Image(), Session.Index(), FFieldMovieCues::EffectDirectory,
			FFieldMovieCues::FirstEffectFile + Movie.Cues);
		if (!File)
		{
			return File.Error();
		}
		CueEffects.emplace(std::move(*File));
		const FLockedGameSound Locked = Session.Sound().Sound()->Lock();
		Locked->StopAllEffects();
		if (!Locked->RegisterEffects(*CueEffects))
		{
			return MakeError("the sound driver takes no more effect files for the movie's cues");
		}
		bCueEffectsRegistered = true;
		return std::nullopt;
	}

	TResult<int32> FFieldMovie::Advance(int32 Vblanks)
	{
		for (int32 Vblank = 0; Vblank < Vblanks; ++Vblank)
		{
			const TResult<bool> Playing = MoviePlayer.Vblank({});
			if (!Playing)
			{
				return Playing.Error();
			}
			if (!bSkipped)
			{
				PlayCues();
			}
		}
		const uint32 Frame = MoviePlayer.FrameNumber();
		if (MoviePlayer.IsFinished() && !bSkipped && !Movie.bRewinds && Frame < Movie.EndFrame)
		{
			// The drive would read on into the sectors after the movie, never reaching the frame: the
			// scripts asked for the other disc's movie.
			return MakeError("the field's movie " + std::to_string(Movie.Movie) + " ends at frame " + std::to_string(Frame) + ", before the frame " +
							 std::to_string(Movie.EndFrame) + " the field waits for");
		}
		return static_cast<int32>(Frame);
	}

	void FFieldMovie::Skip()
	{
		bSkipped = true;
		MoviePlayer.Skip();
	}

	void FFieldMovie::PlayCues()
	{
		// A cue plays once the movie shows its frame, counted from the movie's start frame.
		const uint32 Frame = MoviePlayer.FrameNumber();
		while (NextCue < Cues.size() && static_cast<uint32>(Cues[NextCue].Frame) + Movie.StartFrame <= Frame)
		{
			const FFieldMovieCue& Cue = Cues[NextCue++];
			Session.Sound().Sound()->Lock()->PlayEffectOn(static_cast<uint32>(CueEffects->FileId()) << EffectFileShift | Cue.Effect, Cue.Channel);
		}
	}
}
