#pragma once

#include "CoreMinimal.h"
#include "Field/FieldRenderOptions.h"
#include "Presentation/GameScreen.h"
#include "Screens/GameScreen.h"

#include <memory>

class FMoviePictureFrame;
class FMoviePictureTexture;
class FMovieSubtitleTexture;

namespace Weltall
{
	class FMoviePlayer;
	struct FMovieSubtitlePicture;
}

/** A movie player's picture on the GPU, made again each time the player shows another, and drawn on a game screen. */
class WELTALL_API FMoviePictureView
{
public:
	/** Takes the player's latest picture, brought to the display by Filter. */
	void Follow(const Weltall::FMoviePlayer& Player, Weltall::EMovieFilter Filter);

	/** Lets go of the picture: the next movie starts with none. */
	void Reset();

	TSharedPtr<FMoviePictureTexture> Texture() const { return Picture; }

	/** A displayed frame of the picture on a game screen of Screen's size that shows the picture's Window; black before the first. */
	TSharedRef<const IScreenFrame> Capture(Weltall::FScreenSize Screen, Weltall::FScreenRect Window) const;

private:
	TSharedPtr<FMoviePictureTexture> Picture;
	/** Which of the player's pictures the texture holds. */
	uint32 PictureNumber = 0;
	/** The subtitle line shown with it, kept on the GPU while the line shows. */
	std::shared_ptr<const Weltall::FMovieSubtitlePicture> SubtitleLine;
	TSharedPtr<FMovieSubtitleTexture> Subtitle;
	/** Frames handed out; one the rendering thread no longer holds is filled again. */
	mutable TArray<TSharedRef<FMoviePictureFrame>> Frames;
};
