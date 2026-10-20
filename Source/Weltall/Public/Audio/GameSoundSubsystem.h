#pragma once

#include "CoreMinimal.h"
#include "Sound/GameSoundSession.h"
#include "Sound/UnheardGameSound.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include <memory>

#include "GameSoundSubsystem.generated.h"

class UAudioComponent;

namespace Weltall
{
	class FIso9660;
}

/**
 * The game's sound: one output for the whole session, as the console has one, started with the
 * game and playing from then on. Every screen plays through it, the movies' sound through the
 * SPU's CD input. Without an audio device (a headless run) the sound is made and dropped as time
 * passes, so its songs and effects run on as they would be heard.
 */
UCLASS()
class WELTALL_API UGameSoundSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/**
	 * Starts the game's sound from its disc (which must outlive it) as the game's start-up does, and
	 * plays it on the audio device if there is one; a sound already started stops first, as the
	 * console's restart stops it. False when the disc's sound cannot be read.
	 */
	bool Start(const Weltall::FIso9660& Disc);

	/** The game's sound once started, null before. */
	Weltall::FGameSoundSession* Session() const { return GameSound.get(); }

	/** Holds the sound where it is, nothing made or heard, or lets it go on. */
	void SetHeld(bool bInHeld);

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual TStatId GetStatId() const override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWeltallAudioFocusTestAccess;
	friend class FWeltallRawMovieTestAccess;
#endif
	void Stop();

	std::unique_ptr<Weltall::FGameSoundSession> GameSound;
	std::unique_ptr<Weltall::FUnheardGameSound> Unheard;
	bool bHeld = false;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Output;
};
