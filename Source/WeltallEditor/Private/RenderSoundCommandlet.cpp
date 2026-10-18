#include "RenderSoundCommandlet.h"

#include "Audio.h"
#include "Media/GameLibrary.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/GameSoundSession.h"

DEFINE_LOG_CATEGORY_STATIC(LogRenderSound, Log, All);

namespace
{
	/** What an audio device asks for at a time. */
	constexpr int32 BufferFrames = 1024;
	/** A field script's sound effect: on channel 6, at full volume, centred. */
	constexpr int32 ScriptEffectChannel = 6;
	constexpr int32 ScriptEffectVolume = 0x7F;
	constexpr int32 ScriptEffectPan = 0x40;
}

URenderSoundCommandlet::URenderSoundCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URenderSoundCommandlet::Main(const FString& Params)
{
	FString Out;
	double Seconds = 0.0;
	int32 Song = -1;
	int32 Effect = -1;
	FParse::Value(*Params, TEXT("Out="), Out);
	FParse::Value(*Params, TEXT("Seconds="), Seconds);
	FParse::Value(*Params, TEXT("Song="), Song);
	FParse::Value(*Params, TEXT("Effect="), Effect);
	if (Out.IsEmpty() || Seconds <= 0.0 || (Song < 0) == (Effect < 0))
	{
		UE_LOG(LogRenderSound, Error, TEXT("Usage: -run=RenderSound -Song=N|-Effect=N -Seconds=S -Out=path.wav"));
		return 1;
	}

	const Weltall::FGameLibrary Library = Weltall::FGameLibrary::Scan(std::filesystem::path(*UGameDiscsSubsystem::GamesFolder()));
	const Weltall::FIso9660* Disc = Library.Find(Weltall::EDiscRelease::UsaDisc1);
	if (Disc == nullptr)
	{
		UE_LOG(LogRenderSound, Error, TEXT("Disc 1 is not in the Games folder"));
		return 1;
	}
	const auto Started = Weltall::FGameSoundSession::Start(*Disc);
	if (!Started)
	{
		UE_LOG(LogRenderSound, Error, TEXT("The game's sound cannot start: %hs"), Started.Error().Message.c_str());
		return 1;
	}
	Weltall::FGameSoundSession& Session = **Started;
	Weltall::FGameSoundSystem& Sound = *Session.Sound();
	// The field keeps effect file 0 registered while it runs.
	Sound.Lock()->RegisterEffects(Session.Resident().MainEffects);
	if (Song >= 0)
	{
		const Weltall::TResult<bool> Loaded = Session.FieldMusic().ChangeSong(Song, Weltall::EFieldSongStart::FullVolume);
		if (!Loaded)
		{
			UE_LOG(LogRenderSound, Error, TEXT("Song %d cannot play: %hs"), Song, Loaded.Error().Message.c_str());
			return 1;
		}
	}
	else
	{
		const Weltall::FLockedGameSound Locked = Sound.Lock();
		Locked->StopEffectOn(ScriptEffectChannel);
		Locked->PlayEffectOnVolumePan(static_cast<uint32>(Effect), ScriptEffectChannel, ScriptEffectVolume, ScriptEffectPan);
	}

	const int32 Frames = static_cast<int32>(Seconds * Weltall::IGameSound::SampleRate);
	TArray<int16> Samples;
	Samples.SetNumZeroed(Frames * 2);
	for (int32 Made = 0; Made < Frames; Made += BufferFrames)
	{
		const int32 Count = FMath::Min(BufferFrames, Frames - Made);
		Sound.Render(std::span<int16>(Samples.GetData() + Made * 2, Count * 2));
	}
	TArray<uint8> Wave;
	SerializeWaveFile(Wave, reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16), 2, Weltall::IGameSound::SampleRate);
	if (!FFileHelper::SaveArrayToFile(Wave, *Out))
	{
		UE_LOG(LogRenderSound, Error, TEXT("Cannot write %s"), *Out);
		return 1;
	}
	UE_LOG(LogRenderSound, Display, TEXT("Rendered %s"), *Out);
	return 0;
}
