#include "CoreMinimal.h"

#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS

#include "Audio/GameSoundSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Media/GameMovies.h"
#include "Media/MovieStream.h"
#include "Media/PsxExecutable.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Screens/GameScreenSubsystem.h"
#include "Screens/MovieScreen.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Shell/GameWindowSubsystem.h"
#include "Shell/PlayerSettingsSubsystem.h"

/** Development-only observations of the same movie/audio/screen paths the app uses. */
class FWeltallRawMovieTestAccess
{
public:
	static TSharedPtr<IGameScreen> Screen(UGameScreenSubsystem& Screens) { return Screens.Screen; }
	static bool AudioPlaying(UGameSoundSubsystem& Sound) { return Sound.Output && !Sound.bHeld && !Sound.Output->bIsPaused; }
	static uint32 Frame(const FMovieScreen& Movie) { return Movie.Player.FrameNumber(); }
	static uint32 Pictures(const FMovieScreen& Movie) { return Movie.Player.PictureNumber(); }
};

namespace
{
	class FRawMoviePlaybackCommand final : public IAutomationLatentCommand
	{
	public:
		explicit FRawMoviePlaybackCommand(FAutomationTestBase& InTest) : Test(InTest)
		{
			FParse::Value(FCommandLine::Get(), TEXT("WeltallMovieDisc="), DiscNumber);
			FParse::Value(FCommandLine::Get(), TEXT("WeltallMovieNumber="), MovieNumber);
		}
		~FRawMoviePlaybackCommand()
		{
			if (Screens.IsValid() && Previous) Screens->Show(Previous.ToSharedRef());
		}

		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Phase == 0)
			{
				UGameInstance* Game = nullptr;
				if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* World = Context.World();
					if (World && World->GetGameInstance()) { Game = World->GetGameInstance(); break; }
				}
				if (Game)
				{
					Screens = Game->GetSubsystem<UGameScreenSubsystem>();
					Sound = Game->GetSubsystem<UGameSoundSubsystem>();
					Window = Game->GetSubsystem<UGameWindowSubsystem>();
				}
				if (!Game || !Screens.IsValid() || !Sound.IsValid() || !Window.IsValid() || Window->IsHeldInBackground()
					|| !Sound->Session() || !FWeltallRawMovieTestAccess::Screen(*Screens)
					|| !FWeltallRawMovieTestAccess::AudioPlaying(*Sound))
				{
					if (Now - Started < 20.0) return false;
					Test.AddError(TEXT("Foreground game/audio not ready for raw movie playback"));
					return true;
				}
				if (!Test.TestTrue(TEXT("Movie disc is one or two"), DiscNumber == 1 || DiscNumber == 2)) return true;
				UGameDiscsSubsystem* Discs = Game->GetSubsystem<UGameDiscsSubsystem>();
				const Weltall::EDiscRelease Release = DiscNumber == 2 ? Weltall::EDiscRelease::UsaDisc2 : Weltall::EDiscRelease::UsaDisc1;
				const Weltall::FIso9660* Disc = Discs ? Discs->Find(Release) : nullptr;
				if (!Test.TestNotNull(TEXT("The selected supplied USA disc is available"), Disc)) return true;
				Test.TestTrue(TEXT("Movie reads original 2352-byte sectors"), Disc->Image().IsRaw());
				Test.TestTrue(TEXT("Disc discovery selected the original CUE pair"), Discs->Library().FileOf(Release).extension() == ".cue");
				const auto Exe = Weltall::ReadBootExecutable(*Disc);
				if (!Test.TestTrue(TEXT("Movie disc executable index can be read"), static_cast<bool>(Exe))) return true;
				const auto Index = Weltall::FGameFileIndex::Read(*Exe);
				if (!Test.TestTrue(TEXT("Movie file index can be read"), static_cast<bool>(Index))) return true;
				const auto Location = Weltall::FindMovie(*Index, MovieNumber);
				if (!Test.TestTrue(TEXT("Selected movie exists on this disc"), Location.has_value())) return true;
				// Count the raw stream independently of the rendered player; retain no disc-derived fixture in source.
				Weltall::FMovieStream Stream(Disc->Image(), Location->FirstLba, Location->SectorCount);
				std::vector<Weltall::int16> Samples;
				uint64 SoundSamples = 0, Nonzero = 0;
				for (;;)
				{
					Samples.clear();
					const auto Next = Stream.NextFrame(Samples);
					if (!Next) { Test.AddError(UTF8_TO_TCHAR(Next.Error().Message.c_str())); return true; }
					SoundSamples += Samples.size();
					for (const auto Sample : Samples) Nonzero += Sample != 0;
					if (!Next->has_value()) break;
					++ExpectedPictures;
					ExpectedLastFrame = (**Next).FrameNumber;
				}
				if (!Test.TestTrue(TEXT("Raw movie has video and non-silent XA sound"), ExpectedPictures > 1 && SoundSamples > 0 && Nonzero > 0)) return true;
				const Weltall::EMovieFilter Filter = Game->GetSubsystem<UPlayerSettingsSubsystem>()->Settings().Field.MovieFilter;
				Movie = FMovieScreen::Open(*Disc, MovieNumber, MAX_int32, false, *Sound->Session()->Sound(), Filter);
				if (!Test.TestTrue(TEXT("The production movie screen opens"), Movie.IsValid())) return true;
				Previous = FWeltallRawMovieTestAccess::Screen(*Screens);
				Screens->Show(Movie.ToSharedRef());
				Started = FPlatformTime::Seconds();
				Test.AddInfo(FString::Printf(TEXT("Raw movie visible: disc=%d movie=%d pictures=%u last_frame=%u XA_samples=%llu nonzero=%llu"),
					DiscNumber, MovieNumber, ExpectedPictures, ExpectedLastFrame, SoundSamples, Nonzero));
				Phase = 1;
				return false;
			}
			if (!Screens.IsValid() || !Sound.IsValid() || !Movie) { Test.AddError(TEXT("Movie screen disappeared")); return true; }
			if (Phase == 1)
			{
				if (!Movie->IsFinished())
				{
					if (Now - Started < ExpectedPictures / 15.0 + 30.0) return false;
					Test.AddError(TEXT("Movie did not finish within its raw-stream duration plus timeout margin"));
					return true;
				}
				Test.TestEqual(TEXT("Rendered playback reaches the raw stream's final frame"), FWeltallRawMovieTestAccess::Frame(*Movie), ExpectedLastFrame);
				Test.TestEqual(TEXT("Rendered playback shows every raw movie picture"), FWeltallRawMovieTestAccess::Pictures(*Movie), ExpectedPictures);
				Test.TestTrue(TEXT("Movie plays at its native timing rather than skipping to the end"), Now - Started >= (ExpectedPictures - 1) / 15.0 - 0.25);
				Test.TestTrue(TEXT("The real Android audio output remains active"), FWeltallRawMovieTestAccess::AudioPlaying(*Sound));
				Test.AddInfo(FString::Printf(TEXT("Raw movie finished: frame=%u pictures=%u elapsed=%.3f"),
					FWeltallRawMovieTestAccess::Frame(*Movie), FWeltallRawMovieTestAccess::Pictures(*Movie), Now - Started));
				Started = Now;
				Phase = 2;
			}
			// Retain the final picture briefly so a local app-only capture can compare it with the PC reference.
			return Now - Started >= 3.0;
		}
	private:
		FAutomationTestBase& Test;
		TWeakObjectPtr<UGameScreenSubsystem> Screens;
		TWeakObjectPtr<UGameSoundSubsystem> Sound;
		TWeakObjectPtr<UGameWindowSubsystem> Window;
		TSharedPtr<IGameScreen> Previous;
		TSharedPtr<FMovieScreen> Movie;
		double Started = FPlatformTime::Seconds();
		uint32 ExpectedPictures = 0, ExpectedLastFrame = 0;
		int32 DiscNumber = 2, MovieNumber = 1, Phase = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAndroidRawMoviePlaybackTest, "Weltall.Android.RawMovie.Playback",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FAndroidRawMoviePlaybackTest::RunTest(const FString&)
{
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRawMoviePlaybackCommand>(*this));
	return true;
}

#endif
