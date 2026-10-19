#include "Media/MoviePlayer.h"

#include "Formats/StrBitstream.h"
#include "Sound/GameSoundSystem.h"

namespace Weltall
{
	namespace
	{
		/** The sound read ahead of the picture, and the most pictures decoded ahead to get it. */
		constexpr std::size_t SoundLeadFrames = IGameSound::SampleRate / 2;
		constexpr std::size_t MaxPicturesAhead = 16;
		/** The player's CD volume fades take 40 of the driver's 120 Hz steps, a skip's 10. */
		constexpr int32 FullCdVolume = 0x7FFF;
		constexpr int32 FadeSteps = 40;
		constexpr uint32 FadeOutFrames = 3;
		constexpr int32 SkipFadeSteps = 10;
		constexpr uint16 SkipButtons = FPadState::Bit(EPadButton::Cross) | FPadState::Bit(EPadButton::Start);
		constexpr int32 FirstRepeatDelay = 90;
		/**
		 * The drive delivers 15 pictures a second; a vertical blank is 263 lines of 3413 clocks of the
		 * NTSC GPU's 53,693,175 Hz. Picture n (from 0) is due once Vblanks x 15 x 897619 reaches
		 * n x 53693175.
		 */
		constexpr uint64 PicturesPerSecond = 15;
		constexpr uint64 VblankClocks = 263 * 3413;
		constexpr uint64 GpuClockHz = 53693175;

		uint32 PicturesDue(uint64 Vblanks)
		{
			return static_cast<uint32>(Vblanks * PicturesPerSecond * VblankClocks / GpuClockHz) + 1;
		}
	}

	FMoviePlayer::FMoviePlayer(const FCdImage& InImage, FMovieLocation InLocation, const FMoviePlayback& InPlayback, FGameSoundSystem& InSound,
		std::optional<FMovieSubtitles> InSubtitles)
		: Image(InImage), Location(InLocation), Playback(InPlayback), Sound(InSound), Subtitles(std::move(InSubtitles))
	{
	}

	FMoviePlayer::~FMoviePlayer()
	{
		if (bStarted && !bFinished)
		{
			StopSound();
		}
	}

	TResult<bool> FMoviePlayer::Start()
	{
		if (bStarted)
		{
			return !bFinished;
		}
		bStarted = true;
		Sound.Lock()->SetCdVolume(0, 0);
		Rewind();
		return ShowDue();
	}

	TResult<bool> FMoviePlayer::Vblank(FPadState Held)
	{
		if (!bStarted)
		{
			const TResult<bool> Started = Start();
			if (!Started || !*Started)
			{
				return Started;
			}
		}
		if (bFinished)
		{
			return false;
		}
		if (Playback.bSkippable)
		{
			PollSkip(Held);
		}
		if (StopVblanks > 0 && --StopVblanks == 0)
		{
			Finish();
			return false;
		}
		++Vblanks;
		return bPicturesStopped ? true : ShowDue();
	}

	void FMoviePlayer::Skip()
	{
		if (bFinished || StopVblanks > 0)
		{
			return;
		}
		Sound.Lock()->SetCdVolume(0, SkipFadeSteps);
		// The program only waits out the fade: no picture follows.
		StopVblanks = SkipVblanks;
		Playback.bSkippable = false;
		bPicturesStopped = true;
	}

	void FMoviePlayer::Stop()
	{
		if (bStarted && !bFinished)
		{
			Finish();
		}
	}

	TResult<bool> FMoviePlayer::ShowDue()
	{
		const uint32 Due = PicturesDue(Vblanks);
		while (!bFinished && PicturesShown < Due)
		{
			if (Decoded.empty())
			{
				if (bStreamEnded)
				{
					break;
				}
				const TResult<bool> Next = DecodeNext();
				if (!Next)
				{
					Finish();
					return Next.Error();
				}
				continue;
			}
			ShownFrame = Decoded.front().FrameNumber;
			Shown = std::move(Decoded.front().Picture);
			ShownSubtitle = std::move(Decoded.front().Subtitle);
			Decoded.pop_front();
			++PicturesShown;
			FadeFor(ShownFrame);
			if (ShownFrame >= Playback.EndFrame)
			{
				// The player stops as soon as it shows the end frame, which stays on screen; a rewinding
				// movie reads from its start again instead.
				if (!Playback.bRewinds)
				{
					Finish();
					return false;
				}
				Rewind();
			}
		}
		if (bFinished)
		{
			return false;
		}
		if (const TResult<bool> Read = ReadAhead(); !Read)
		{
			Finish();
			return Read.Error();
		}
		if (bStreamEnded && Decoded.empty() && PicturesDue(Vblanks) > PicturesShown)
		{
			// The last picture has had its time on screen.
			Finish();
		}
		return !bFinished;
	}

	TResult<bool> FMoviePlayer::ReadAhead()
	{
		std::size_t Queued = Sound.Lock().Spu().QueuedCdInput();
		while (!bStreamEnded && Queued < SoundLeadFrames && Decoded.size() < MaxPicturesAhead)
		{
			const TResult<bool> Next = DecodeNext();
			if (!Next)
			{
				return Next.Error();
			}
			Queued = Sound.Lock().Spu().QueuedCdInput();
		}
		return true;
	}

	void FMoviePlayer::FadeFor(uint32 Frame)
	{
		if (!bFadedIn && Frame > Playback.StartFrame)
		{
			bFadedIn = true;
			Sound.Lock()->SetCdVolume(FullCdVolume, FadeSteps);
		}
		if (!bFadingOut && Frame + FadeOutFrames >= Playback.EndFrame)
		{
			bFadingOut = true;
			Sound.Lock()->SetCdVolume(0, FadeSteps);
		}
	}

	void FMoviePlayer::Rewind()
	{
		// Reading starts over at the start sector: the drive's sound is cut and silent until the next fade-in.
		if (Stream)
		{
			StopSound();
		}
		const uint32 First = Location.FirstLba + std::min(Playback.StartSector, Location.SectorCount);
		Stream.emplace(Image, First, Location.SectorCount - (First - Location.FirstLba));
		Decoded.clear();
		// The undub's subtitles start again as the player asks for the movie's file again.
		if (Subtitles)
		{
			Subtitles->Restart();
		}
		bStreamEnded = false;
		bFadedIn = false;
		bFadingOut = false;
	}

	void FMoviePlayer::StopSound()
	{
		const FLockedGameSound Locked = Sound.Lock();
		Locked->SetCdVolume(0, 0);
		Locked.Spu().ClearCdInput();
	}

	void FMoviePlayer::Finish()
	{
		bFinished = true;
		Decoded.clear();
		StopSound();
	}

	TResult<bool> FMoviePlayer::DecodeNext()
	{
		SoundScratch.clear();
		TResult<std::optional<FStrFrame>> Frame = Stream->NextFrame(SoundScratch);
		if (!Frame)
		{
			return Frame.Error();
		}
		if (Playback.bSound)
		{
			Sound.Lock().Spu().QueueCdInput(SoundScratch);
		}
		if (!Frame->has_value())
		{
			bStreamEnded = true;
			return false;
		}
		const FStrFrame& Compressed = **Frame;
		if (Compressed.FrameNumber < Playback.StartFrame)
		{
			return true;
		}
		const TResult<std::vector<uint16>> Codes = DecodeStrBitstream(Compressed.Bitstream, Compressed.Width, Compressed.Height);
		if (!Codes)
		{
			return Codes.Error();
		}
		TResult<FMdecPicture> Picture = DecodeMdecPicture(*Codes, Compressed.Width, Compressed.Height, FMdecTables::Standard());
		if (!Picture)
		{
			return Picture.Error();
		}
		Decoded.push_back({std::make_shared<const FMdecPicture>(std::move(*Picture)), Compressed.FrameNumber,
			Subtitles ? Subtitles->At(Compressed.FrameNumber) : nullptr});
		return true;
	}

	void FMoviePlayer::PollSkip(FPadState Buttons)
	{
		// The program reads the pad with an auto-repeat: buttons held unchanged count as pressed again
		// once they have been read unchanged more than 90 times, then every other reading.
		uint32 Previous = Polled;
		Polled = Buttons.Buttons;
		if (Previous == Polled && Polled != 0)
		{
			if (++RepeatCount > RepeatDelay)
			{
				Previous = 0;
				RepeatCount = 0;
				RepeatDelay = 1;
			}
		}
		else
		{
			RepeatCount = 0;
			RepeatDelay = FirstRepeatDelay;
		}
		const uint32 Newly = Polled & ~Previous;
		if (StopVblanks == 0 && (Newly & SkipButtons) != 0)
		{
			Sound.Lock()->SetCdVolume(0, SkipFadeSteps);
			StopVblanks = SkipVblanks;
		}
	}
}
