#pragma once

#include "CoreMinimal.h"
#include "Media/GameLibrary.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Misc/Paths.h"
#include "Screens/GameLoopScreen.h"
#include "Sound/GameSoundSession.h"
#include "Sound/UnheardGameSound.h"

#include <filesystem>
#include <memory>
#include <optional>

/** The configured first disc (-Games= or the project's Games folder), or null when it is absent. */
inline const Weltall::FIso9660* FindTestDisc(Weltall::FGameLibrary& Library)
{
	Library = Weltall::FGameLibrary::Scan(std::filesystem::path(*UGameDiscsSubsystem::GamesFolder()));
	return Library.Find(Weltall::EDiscRelease::UsaDisc1);
}

/** The game on a field map with its sound (made and dropped as time passes), played a display frame at a time at 60 a second. */
struct FGameLoopTestGame
{
	static constexpr double FrameSeconds = 1.0 / 60.0;

	std::unique_ptr<Weltall::FGameSoundSession> Sound;
	std::unique_ptr<Weltall::FUnheardGameSound> Unheard;
	TSharedPtr<FGameLoopScreen> Screen;

	/** A new game entering field map Map, as Staged leaves it; the reason when it cannot start. */
	std::optional<Weltall::FError> Start(const Weltall::FIso9660& Disc, uint16 Map, FGameLoopScreen::FStateStaged Staged)
	{
		auto Started = FGameLoopScreen::EnterField(Disc,
			[this, &Disc]() -> Weltall::FGameSoundSession*
			{
				Unheard.reset();
				auto Made = Weltall::FGameSoundSession::Start(Disc);
				Sound = Made ? std::move(*Made) : nullptr;
				if (Sound)
				{
					Unheard = std::make_unique<Weltall::FUnheardGameSound>(*Sound->Sound());
				}
				return Sound.get();
			},
			Map, Weltall::FPlayerSettings{}, MoveTemp(Staged));
		if (!Started)
		{
			return Started.Error();
		}
		Screen = *Started;
		return std::nullopt;
	}

	void Frame(Weltall::FPadState Held = {})
	{
		Screen->Update(FrameSeconds, Held);
		if (Unheard)
		{
			Unheard->Advance(FrameSeconds);
		}
		Screen->CaptureFrame();
	}

	/** Seconds of display frames with Held down. */
	void Run(double Seconds, Weltall::FPadState Held = {})
	{
		for (int32 Frames = FMath::RoundToInt32(Seconds / FrameSeconds); Frames > 0; --Frames)
		{
			Frame(Held);
		}
	}
};
