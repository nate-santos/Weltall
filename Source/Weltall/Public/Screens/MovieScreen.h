#pragma once

#include "CoreMinimal.h"
#include "Input/PadLatch.h"
#include "Media/MoviePlayer.h"
#include "Screens/GameScreen.h"
#include "Screens/MoviePictureView.h"
#include "Timing/TickClock.h"

namespace Weltall
{
	class FGameSoundSystem;
	class FIso9660;
}

/**
 * A movie on its own, as the Movie program shows it: the display window shows the picture's top 216
 * lines, 10 lines down a 320 x 240 screen.
 */
class WELTALL_API FMovieScreen final : public IGameScreen
{
public:
	/** The Movie program's display: a 320 x 240 screen whose window shows 216 lines, 10 lines down. */
	static constexpr Weltall::FScreenSize Screen{320, 240};
	static constexpr Weltall::FScreenRect Window{0, 10, 320, 216};

	/**
	 * Movie Number of a disc played to EndFrame, its sound into the game's, its pictures brought to the
	 * display by Filter, with the disc's own subtitles when it is the undub's; the disc and the sound must
	 * outlive the screen. Null when the disc has no such movie.
	 */
	static TSharedPtr<FMovieScreen> Open(const Weltall::FIso9660& Disc, int32 Number, uint32 EndFrame, bool bSkippable, Weltall::FGameSoundSystem& Sound,
		Weltall::EMovieFilter Filter);

	FMovieScreen(const Weltall::FCdImage& Image, Weltall::FMovieLocation Location, uint32 EndFrame, bool bSkippable, Weltall::FGameSoundSystem& Sound,
		Weltall::EMovieFilter InFilter, std::optional<Weltall::FMovieSubtitles> Subtitles = std::nullopt);

	virtual void Update(double DeltaSeconds, Weltall::FPadState Held) override;
	virtual TSharedRef<const IScreenFrame> CaptureFrame() const override;

	bool IsFinished() const { return Player.IsFinished(); }

private:
	Weltall::FMoviePlayer Player;
	Weltall::FTickClock Vblanks;
	Weltall::FPadLatch Latch;
	FMoviePictureView Picture;
	Weltall::EMovieFilter Filter;
};
