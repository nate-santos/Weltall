#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "GameScreenSubsystem.generated.h"

class IGameScreen;
class IScreenFrame;

/**
 * The screen the game shows: it runs every frame with the player's controller and the viewport
 * draws what it shows. Until a screen is opened the viewport stays black.
 */
UCLASS()
class WELTALL_API UGameScreenSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Shows a screen in place of the current one. */
	void Show(TSharedRef<IGameScreen> InScreen);

	/** Lets go of the screen: nothing runs or draws any more. */
	void Close();

	/** What the current screen shows now, if there is one. */
	TSharedPtr<const IScreenFrame> CaptureFrame() const;

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual TStatId GetStatId() const override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWeltallAudioFocusTestAccess;
#endif
	TSharedPtr<IGameScreen> Screen;
};
