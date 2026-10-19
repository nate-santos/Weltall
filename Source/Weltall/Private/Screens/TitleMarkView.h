#pragma once

#include "CoreMinimal.h"
#include "Game/TitleMarkDrawing.h"
#include "Screens/GameScreen.h"

#include <string>

class FTitleMarkFrame;
class FVramTexture;

namespace Weltall
{
	class FFieldProgram;
	class FGameSession;
}

/** The port's name and version on the title screen (Weltall::FTitleMarkDrawing), over the title's picture and its menu. */
class FTitleMarkView
{
public:
	FTitleMarkView();
	~FTitleMarkView();

	/** Background with the mark over it while the field shows the title (its map, no movie); Background itself otherwise. */
	TSharedRef<const IScreenFrame> Capture(const Weltall::FGameSession& Session, const Weltall::FFieldProgram& Field, TSharedRef<const IScreenFrame> Background,
		bool bUnderMenu = false) const;

private:
	std::string Version;
	mutable Weltall::FTitleMarkDrawing Drawing;
	TSharedRef<FVramTexture> Vram;
	/** Frames handed out; one the rendering thread no longer holds is filled again. */
	mutable TArray<TSharedRef<FTitleMarkFrame>> DisplayFrames;
};
