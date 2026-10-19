#include "Game/MovieProgram.h"

#include "Game/GameSession.h"
#include "Sound/GameSoundSession.h"

#include <string>

namespace Weltall
{
	namespace
	{
		/** The request's kinds: the movie with its sound, and its sound alone, which the retail program never plays. */
		constexpr uint8 WithSound = 1;
		constexpr uint8 SoundAlone = 2;
		/** L1, R1, Start and Select together start the console again. */
		constexpr uint16 SoftResetButtons = 0x090C;
	}

	TResult<std::unique_ptr<FMovieProgram>> FMovieProgram::Start(FGameSession& Session)
	{
		std::unique_ptr<FMovieProgram> Program(new FMovieProgram(Session));
		const FMovieProgramRequest& Request = Session.Kernel().Movie;
		// The CD volume goes to nothing before anything plays.
		Session.Sound().Sound()->Lock()->SetCdVolume(0, 0);
		if (Request.Type == FMovieProgramRequest::NoMovie)
		{
			return MakeError(std::string(GameProgramName(EGameProgram::Movie)) + " asked for no movie shows its debug menu, which the port does not run");
		}
		const uint8 Kind = Request.Type & FMovieProgramRequest::KindBits;
		if (Kind == SoundAlone)
		{
			Program->End();
			return Program;
		}
		if (Kind != WithSound)
		{
			return MakeError(std::string(GameProgramName(EGameProgram::Movie)) + ": movie " + std::to_string(Request.Number) + " of kind " +
							 std::to_string(Kind) + " is read from a layout of the disc the retail game does not have");
		}
		const TResult<FGameDisc> Disc = Session.MovieDisc();
		if (!Disc)
		{
			return Disc.Error();
		}
		const std::optional<FMovieLocation> Location = FindMovie(*Disc->Index, Request.Number);
		if (!Location)
		{
			// Past the disc's movies: none is played, and the program goes on to the next.
			Program->End();
			return Program;
		}
		if (Location->SectorCount == 0)
		{
			// The disc's stand-in for a movie, too short to hold a sector.
			Session.Kernel().Movie.NextProgram = EGameProgram::DebugMenu;
			Program->End();
			return Program;
		}
		FMoviePlayback Playback;
		Playback.EndFrame = (Request.Type & FMovieProgramRequest::EndFrameGiven) != 0 ? Request.EndFrame : FMoviePlayback::DefaultEndFrame;
		Playback.bSkippable = Request.NoSkip == 0;
		TResult<std::optional<FMovieSubtitles>> Subtitles = Session.MovieSubtitles(Request.Number);
		if (!Subtitles)
		{
			return Subtitles.Error();
		}
		Program->MoviePlayer = std::make_unique<FMoviePlayer>(*Disc->Image, *Location, Playback, *Session.Sound().Sound(), std::move(*Subtitles));
		const TResult<bool> Started = Program->MoviePlayer->Start();
		if (!Started)
		{
			return Started.Error();
		}
		if (!*Started)
		{
			Program->End();
		}
		return Program;
	}

	std::optional<FError> FMovieProgram::Frame(std::span<const FPadState> HeldPerVblank)
	{
		if (ProgramState != EGameProgramState::Running)
		{
			return MakeError(std::string(GameProgramName(EGameProgram::Movie)) + " has ended");
		}
		for (const FPadState Held : HeldPerVblank)
		{
			Session.Vblank(Held);
			const TResult<bool> Playing = MoviePlayer->Vblank(Held);
			if (!Playing)
			{
				return Playing.Error();
			}
			if (Session.Pad().Held() == SoftResetButtons)
			{
				MoviePlayer->Stop();
				ProgramState = EGameProgramState::SoftReset;
				return std::nullopt;
			}
			if (!*Playing)
			{
				End();
				return std::nullopt;
			}
		}
		return std::nullopt;
	}

	void FMovieProgram::End()
	{
		Session.Kernel().NextProgram = Session.Kernel().Movie.NextProgram;
		ProgramState = EGameProgramState::Ended;
	}
}
