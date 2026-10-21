#include "Audio/GameSoundWave.h"

#include "Sound/GameSound.h"

UGameSoundWave::UGameSoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 2;
	SetSampleRate(Weltall::IGameSound::SampleRate);
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	// The game's sound code goes on while its sound is silent, as it does on the console.
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	bCanProcessAsync = true;
}

void UGameSoundWave::SetSound(std::shared_ptr<Weltall::IGameSound> InSound)
{
	Sound = MoveTemp(InSound);
}

int32 UGameSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
#if PLATFORM_ANDROID && WITH_DEV_AUTOMATION_TESTS
	GeneratedSamples.fetch_add(static_cast<uint64>(NumSamples), std::memory_order_relaxed);
#endif
	OutAudio.Reset();
	OutAudio.AddZeroed(NumSamples * sizeof(int16));
	if (Sound)
	{
		Sound->Render(std::span<int16>(reinterpret_cast<int16*>(OutAudio.GetData()), NumSamples));
	}
	return NumSamples;
}
