#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameModeBase.h"

#include "WeltallGameMode.generated.h"

class SBorder;
class STextBlock;

/**
 * The game draws its own screens, so the player has a controller but never a pawn in a world. Play
 * begins as the game does from disc 1, or from disc 2 when the player has only that, as the console
 * allows: the console powered on, its notice, the opening movie, then the programs as the game's loop
 * runs them. Without either disc the player is told where to put them, and the game starts once one is
 * there. A game that stops, or cannot start, says why and where its log is.
 */
UCLASS()
class AWeltallGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWeltallGameMode();

	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

protected:
	virtual void BeginPlay() override;
	/** The game's screen goes before the sound it plays through. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Powers the game on from disc 1, or from disc 2 when it is the only one; false when the player's discs have neither. */
	bool StartGame();
	/** Looks in the Games folder again while both discs are missing; starts the game once one is there. */
	void LookForDiscs();
	/** What the player is told while both discs are missing: where they go, what was found and what was not used. */
	FText DiscsMissing() const;
	/** A message over the viewport in place of the game's picture, or over it, darkened (bOverPicture), until hidden. */
	void ShowNotice(const FText& Text, bool bOverPicture);
	void HideNotice();

	/** The game follows the player's settings as they change. */
	FDelegateHandle SettingsChanged;
	FTimerHandle LookAgain;
	TSharedPtr<SBorder> Notice;
	TSharedPtr<STextBlock> NoticeText;
};
