#include "Battle/Show/BattleGameSound.h"

#include "Foundation/LittleEndian.h"
#include "Sound/GameSoundSystem.h"
#include "Sound/ResidentSound.h"
#include "Sound/SoundFiles.h"

#include <algorithm>
#include <string>

namespace Weltall
{
	namespace
	{
		/** Where an effect file keeps its set's id. */
		constexpr std::size_t GameSoundEffectFileId = 0x14;
	}

	FBattleGameSound::FBattleGameSound(FGameSoundSystem& InSound, FResidentSound& InResident, FSoundSequence* InMusic)
		: Sound(InSound), Resident(InResident), Music(InMusic)
	{
	}

	FBattleGameSound::~FBattleGameSound()
	{
		const FLockedGameSound Locked = Sound.Lock();
		for (const FRegistered& File : Registered)
		{
			Locked->UnregisterEffects(*File.File);
		}
	}

	std::optional<FError> FBattleGameSound::TakeError()
	{
		std::optional<FError> Taken = std::move(Error);
		Error.reset();
		return Taken;
	}

	void FBattleGameSound::Fail(FError InError)
	{
		if (!Error)
		{
			Error = std::move(InError);
		}
	}

	void FBattleGameSound::PlayEffect(uint32 Effect)
	{
		Sound.Lock()->PlayEffect(Effect);
	}

	void FBattleGameSound::PlaySystemEffect(uint32 Effect)
	{
		Sound.Lock()->PlaySystemEffect(Effect);
	}

	void FBattleGameSound::StopEffect(uint32 Effect)
	{
		Sound.Lock()->StopEffect(Effect);
	}

	void FBattleGameSound::PlayEffectOn(uint32 Effect, int32 Voice)
	{
		Sound.Lock()->PlayEffectOn(Effect, Voice);
	}

	void FBattleGameSound::PlayEffectAt(uint32 Effect, int32 Volume, int32 Pan)
	{
		Sound.Lock()->PlayEffectVolumePan(Effect, Volume, Pan);
	}

	bool FBattleGameSound::IsEffectPlaying(uint32 Effect)
	{
		return Sound.Lock()->PlayingEffects(Effect) != 0;
	}

	void FBattleGameSound::SetEffectVolume(uint32 Effect, int32 Volume)
	{
		Sound.Lock()->SetEffectVolume(Effect, Volume);
	}

	void FBattleGameSound::FadeEffectVolume(uint32 Effect, int32 Volume, int32 Steps)
	{
		Sound.Lock()->FadeEffectVolume(Effect, Volume, Steps);
	}

	void FBattleGameSound::SetMusicVolume(int32 Volume, int32 Steps)
	{
		if (Music != nullptr)
		{
			Sound.Lock()->SetSequenceVolume(*Music, Volume, Steps);
		}
	}

	void FBattleGameSound::RegisterEffects(std::span<const uint8> Seds)
	{
		TResult<FSoundEffectFile> Parsed = FSoundEffectFile::Parse(std::vector<uint8>(Seds.begin(), Seds.end()));
		if (!Parsed)
		{
			Fail(Parsed.Error());
			return;
		}
		auto File = std::make_unique<FSoundEffectFile>(std::move(*Parsed));
		const FLockedGameSound Locked = Sound.Lock();
		if (!Locked->RegisterEffects(*File))
		{
			// The driver refuses a set whose id it holds already, as the original's does; any other refusal is the port's room.
			if (Locked->FindEffects(File->FileId()) == nullptr)
			{
				Fail(MakeError("the sound driver has no room for the battle's effect set " + std::to_string(File->FileId())));
			}
			return;
		}
		Registered.push_back({Seds.data(), std::move(File)});
	}

	void FBattleGameSound::UnregisterEffects(std::span<const uint8> Seds)
	{
		const auto Found = std::find_if(Registered.begin(), Registered.end(), [&Seds](const FRegistered& File) { return File.Source == Seds.data(); });
		if (Found == Registered.end())
		{
			return;
		}
		Sound.Lock()->UnregisterEffects(*Found->File);
		Registered.erase(Found);
	}

	void FBattleGameSound::StopAllEffects()
	{
		Sound.Lock()->StopAllEffects();
	}

	void FBattleGameSound::StopEffects(std::span<const uint8> Seds)
	{
		if (Seds.size() < GameSoundEffectFileId + 2)
		{
			return;
		}
		const FLockedGameSound Locked = Sound.Lock();
		if (const FSoundEffectFile* File = Locked->FindEffects(LittleEndian::ReadU16(Seds, GameSoundEffectFileId)))
		{
			Locked->StopEffectsOfFile(*File);
		}
	}

	bool FBattleGameSound::HasEffects(uint16 Id)
	{
		return Sound.Lock()->FindEffects(Id) != nullptr;
	}

	void FBattleGameSound::UnloadGearBattleBank()
	{
		if (Resident.GearBattleBank != nullptr)
		{
			Sound.Lock()->UnloadBank(*Resident.GearBattleBank);
			Resident.GearBattleBank = nullptr;
		}
	}

	void FBattleGameSound::UnloadBattleBank()
	{
		if (BattleBank != nullptr)
		{
			Sound.Lock()->UnloadBank(*BattleBank);
			BattleBank = nullptr;
		}
	}

	void FBattleGameSound::LoadBattleBank(std::span<const uint8> Wds)
	{
		BattleBank = Load(Wds, false);
	}

	void FBattleGameSound::LoadBattleVoiceBank(std::span<const uint8> Wds)
	{
		BattleBank = Load(Wds, true);
	}

	void FBattleGameSound::LoadGearBattleBank(std::span<const uint8> Wds)
	{
		Resident.GearBattleBank = Load(Wds, false);
	}

	const FSoundBank* FBattleGameSound::Load(std::span<const uint8> Wds, bool bVoices)
	{
		TResult<FSoundBankFile> Parsed = FSoundBankFile::Parse(std::vector<uint8>(Wds.begin(), Wds.end()));
		if (!Parsed)
		{
			Fail(Parsed.Error());
			return nullptr;
		}
		const FLockedGameSound Locked = Sound.Lock();
		const FSoundBank* Bank = bVoices ? Locked->LoadVoiceBank(*Parsed) : Locked->LoadBank(*Parsed);
		if (Bank == nullptr)
		{
			Fail(MakeError("the battle's sound bank " + std::to_string(Parsed->Id()) + " finds no room in sound memory"));
		}
		return Bank;
	}

	bool FBattleGameSound::HasBank(uint16 Id)
	{
		return Sound.Lock()->FindBank(Id) != nullptr;
	}

	void FBattleGameSound::PauseSound()
	{
		Sound.Lock()->Pause();
	}

	void FBattleGameSound::ResumeSound()
	{
		Sound.Lock()->Resume();
	}
}
