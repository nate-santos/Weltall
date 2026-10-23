#include "Shell/GameDiscsSubsystem.h"

#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "WeltallLog.h"

FString UGameDiscsSubsystem::GamesFolder()
{
	FString Given;
	if (FParse::Value(FCommandLine::Get(), TEXT("-Games="), Given))
	{
		return FPaths::ConvertRelativePathToFull(Given);
	}
#if PLATFORM_ANDROID
	return FBundledDiscsPreparation::GamesFolder();
#else
	// A packaged game's root holds its executable; the project's folder lies a level below it.
	const FString Beside = FPlatformProperties::RequiresCookedData() ? FPaths::RootDir() : FPaths::ProjectDir();
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(Beside, TEXT("Games")));
#endif
}

void UGameDiscsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Given;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-Games="), Given)) Preparation.Begin();
	if (Preparation.IsBlocked()) return;
	Discs = Preparation.UsesPackagedDiscs() ? Preparation.TakeLibrary() : Weltall::FGameLibrary::Scan(std::filesystem::path(*GamesFolder()));
	UE_LOG(LogWeltall, Log, TEXT("Found %d of the game's discs in %s"), static_cast<int32>(Discs.Releases().size()), *GamesFolder());
	LogNotes();
}

void UGameDiscsSubsystem::Deinitialize()
{
	Preparation.Stop();
	Super::Deinitialize();
}

const Weltall::FIso9660* UGameDiscsSubsystem::Find(Weltall::EDiscRelease Release) const
{
	return Discs.Find(Release);
}

void UGameDiscsSubsystem::Rescan()
{
	Preparation.Poll();
	if (Preparation.IsBlocked()) return;
	const std::size_t Before = Discs.Releases().size();
	if (Preparation.UsesPackagedDiscs())
	{
		if (Preparation.HasLibrary()) Discs.Adopt(Preparation.TakeLibrary());
	}
	else Discs.Adopt(Weltall::FGameLibrary::Scan(std::filesystem::path(*GamesFolder())));
	if (Discs.Releases().size() != Before)
	{
		UE_LOG(LogWeltall, Log, TEXT("Found %d of the game's discs in %s"), static_cast<int32>(Discs.Releases().size()), *GamesFolder());
		LogNotes();
	}
}

void UGameDiscsSubsystem::LogNotes() const
{
	for (const Weltall::FLibraryNote& Note : Discs.Notes())
	{
		const std::u8string File = std::filesystem::path(Note.File).make_preferred().u8string();
		UE_LOG(LogWeltall, Warning, TEXT("%s: %hs"), UTF8_TO_TCHAR(reinterpret_cast<const char*>(File.c_str())), Note.Text.c_str());
	}
}
