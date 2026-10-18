#include "Shell/PlayerSettingsSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Shell/PlayerFolders.h"
#include "WeltallLog.h"

#include <filesystem>
#include <system_error>

namespace
{
	/** What the game changes by itself (the framing, the battle speed) is written once it has rested this long. */
	constexpr float CameraRestSeconds = 2.0f;
}

FString UPlayerSettingsSubsystem::SettingsFile()
{
	return FPaths::Combine(PlayerSettingsFolder(), TEXT("Settings.txt"));
}

void UPlayerSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *SettingsFile()))
	{
		Current = Weltall::ReadPlayerSettings(TCHAR_TO_UTF8(*Text));
	}
	else
	{
#if PLATFORM_ANDROID
		// Start with the simplest custom render path; MSAA/upscalers need mobile GPU validation.
		Current.Field.AntiAliasing = Weltall::EFieldAntiAliasing::None;
		Current.Field.Filter = Weltall::EFieldTextureFilter::Nearest;
		Current.Field.SpriteFilter = Weltall::EFieldTextureFilter::Nearest;
		Current.Field.MovieFilter = Weltall::EMovieFilter::Smooth;
#endif
		// The first start writes the defaults, so the file lists every setting there is.
		Save();
	}
}

void UPlayerSettingsSubsystem::Deinitialize()
{
	if (UnsavedFor.IsSet())
	{
		Save();
	}
	Super::Deinitialize();
}

void UPlayerSettingsSubsystem::Change(const Weltall::FPlayerSettings& Settings)
{
	Current = Settings;
	Save();
	OnSettingsChanged.Broadcast();
}

void UPlayerSettingsSubsystem::Follow(const Weltall::FPlayerSettings& Settings)
{
	Current = Settings;
	OnSettingsChanged.Broadcast();
}

void UPlayerSettingsSubsystem::KeepCameraOffsets(const Weltall::FFieldCameraOffsets& Field, const Weltall::FWorldMapCameraOffsets& WorldMap)
{
	if (Field != Current.Camera || WorldMap != Current.WorldMapCamera)
	{
		Current.Camera = Field;
		Current.WorldMapCamera = WorldMap;
		UnsavedFor = 0.0f;
	}
}

void UPlayerSettingsSubsystem::KeepBattleSpeed(Weltall::EBattleSpeed Speed)
{
	if (Speed != Current.BattleSpeed)
	{
		Current.BattleSpeed = Speed;
		UnsavedFor = 0.0f;
	}
}

void UPlayerSettingsSubsystem::Save()
{
	UnsavedFor.Reset();
	const FString Target = SettingsFile();
	const FString Written = Target + TEXT(".new");
	const std::string Text = Weltall::WritePlayerSettings(Current);
	IFileManager::Get().MakeDirectory(*PlayerSettingsFolder(), true);
	if (!FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Text.c_str())), *Written, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogWeltall, Error, TEXT("The settings cannot be written to %s"), *Written);
		return;
	}
	// Renaming over the old file replaces it in one step on every platform.
	std::error_code Error;
	std::filesystem::rename(std::filesystem::path(*Written), std::filesystem::path(*Target), Error);
	if (Error)
	{
		UE_LOG(LogWeltall, Error, TEXT("The settings cannot be put in place at %s: %hs"), *Target, Error.message().c_str());
	}
}

void UPlayerSettingsSubsystem::Tick(float DeltaTime)
{
	*UnsavedFor += DeltaTime;
	if (*UnsavedFor >= CameraRestSeconds)
	{
		Save();
	}
}

ETickableTickType UPlayerSettingsSubsystem::GetTickableTickType() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UPlayerSettingsSubsystem::IsTickable() const
{
	return UnsavedFor.IsSet();
}

UWorld* UPlayerSettingsSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance()->GetWorld();
}

TStatId UPlayerSettingsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPlayerSettingsSubsystem, STATGROUP_Tickables);
}
