#pragma once

#include "CoreMinimal.h"
#include "Media/Iso9660.h"
#include "Presentation/DisplayOptions.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameWindowSubsystem.generated.h"

/**
 * The game's window: the sizes the display offers it, the size the player chose in the settings or the whole
 * display without a border, which the window takes as the game starts and whenever the choice changes, and what
 * the game does while its window is not the one in use.
 */
UCLASS()
class WELTALL_API UGameWindowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The display's resolutions a window fits on and the window's size now; none where the game has no window of its own. */
	Weltall::FDisplayModes Modes() const;

	/** The game is to stand still: its window is not the one in use, and the settings pause it there. */
	bool IsHeldInBackground() const;

	/** Drops the first mobile frame after resuming instead of simulating time spent suspended. */
	bool ConsumeResumeFrame();

	/** The window's icon becomes the game's own, read from the player's disc (Windows; elsewhere the window keeps its own). */
	void ShowGameIcon(const Weltall::FIso9660& Disc);

private:
	/**
	 * The window takes the size the settings have, or the whole display, once for each choice (the player may size it
	 * otherwise after), and the sound is heard in the background or not.
	 */
	void Follow();
	void ApplicationBackgrounded();
	void ApplicationForegrounded();
	void ApplicationDeactivated();
	void ApplicationReactivated();
	void HoldMobileAudio();

	FDelegateHandle SettingsChanged;
	FDelegateHandle BackgroundedHandle;
	FDelegateHandle ForegroundedHandle;
	FDelegateHandle DeactivatedHandle;
	FDelegateHandle ReactivatedHandle;
	bool bApplicationForeground = true;
	bool bApplicationActive = true;
	bool bResumeFrame = false;
	Weltall::FDisplaySize Followed;
	bool bFollowedFullScreen = false;
};
