#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"

#include <memory>
#include <atomic>

#include "GameSoundWave.generated.h"

namespace Weltall
{
	class IGameSound;
}

/**
 * A game's sound as the audio mixer plays it: 44.1 kHz stereo made on the audio render thread by the
 * game's own sound, for as long as the wave plays.
 */
UCLASS()
class WELTALL_API UGameSoundWave final : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UGameSoundWave(const FObjectInitializer& ObjectInitializer);

	/** The sound the wave plays; set before it starts. */
	void SetSound(std::shared_ptr<Weltall::IGameSound> InSound);

	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS
	friend class FWeltallAudioFocusTestAccess;
	std::atomic<uint64> GeneratedSamples{0};
#endif
	std::shared_ptr<Weltall::IGameSound> Sound;
};
