#include "Sound/GameSoundSystem.h"

#include <algorithm>

namespace Weltall
{
	FGameSoundSystem::FGameSoundSystem(const FSoundDriverTables& Tables, const FSoundDriverOptions& Options)
		: Driver(Spu, Tables, Options)
	{
	}

	FLockedGameSound FGameSoundSystem::Lock()
	{
		return FLockedGameSound(Mutex, Driver, Spu);
	}

	void FGameSoundSystem::SetVolumes(const FSoundVolumes& Volumes)
	{
		const std::lock_guard<std::mutex> Guard(Mutex);
		MasterGain = SoundVolumeGain(Volumes.Master);
		KindGains[static_cast<std::size_t>(ESoundKind::Music)] = SoundVolumeGain(Volumes.Music);
		KindGains[static_cast<std::size_t>(ESoundKind::Effect)] = SoundVolumeGain(Volumes.Effects);
		KindGains[static_cast<std::size_t>(ESoundKind::Voice)] = SoundVolumeGain(Volumes.Voices);
		Spu.SetCdGain(SoundVolumeGain(Volumes.Movies));
		SetVoiceGains();
	}

	void FGameSoundSystem::SetVoiceGains()
	{
		for (int32 Voice = 0; Voice < FSpu::VoiceCount; ++Voice)
		{
			Spu.SetVoiceGain(Voice, KindGains[static_cast<std::size_t>(Driver.VoiceKind(Voice))]);
		}
	}

	void FGameSoundSystem::Render(std::span<int16> Stereo)
	{
		const std::size_t Frames = Stereo.size() / 2;
		std::size_t Made = 0;
		while (Made < Frames)
		{
			const std::lock_guard<std::mutex> Guard(Mutex);
			if (Clock.IsInterruptDue())
			{
				Driver.Interrupt();
				Clock.RunInterrupt();
				SetVoiceGains();
			}
			const std::size_t Count = std::min(Frames - Made, static_cast<std::size_t>(Clock.SamplesToInterrupt()));
			const std::span<int16> Samples = Stereo.subspan(Made * 2, Count * 2);
			Spu.Render(Samples);
			Driver.MixReplacementMusic(Samples, KindGains[static_cast<std::size_t>(ESoundKind::Music)]);
			if (MasterGain != FSpu::FullGain)
			{
				for (int16& Sample : Samples)
				{
					Sample = static_cast<int16>(Sample * MasterGain >> 15);
				}
			}
			Clock.MakeSamples(static_cast<int32>(Count));
			Made += Count;
		}
	}
}
