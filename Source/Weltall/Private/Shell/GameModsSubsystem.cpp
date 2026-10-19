#include "Shell/GameModsSubsystem.h"

#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Shell/PlayerFolders.h"
#include "Shell/PlayerSettingsSubsystem.h"
#include "WeltallLog.h"

FString UGameModsSubsystem::ModsFolder()
{
#if PLATFORM_ANDROID
	// Mod inputs survive bundled-disc version changes in the player's private data folder.
	return PlayerDataFolder() / TEXT("Mods");
#else
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GetPath(UGameDiscsSubsystem::GamesFolder()), TEXT("Mods")));
#endif
}

void UGameModsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGameDiscsSubsystem>();
	Collection.InitializeDependency<UPlayerSettingsSubsystem>();
	Found = Weltall::FindMods(std::filesystem::path(*ModsFolder()));
	InUse = GetGameInstance()->GetSubsystem<UPlayerSettingsSubsystem>()->Settings().PerfectWorks;
	if (Found.PerfectWorks)
	{
		UE_LOG(LogWeltall, Log, TEXT("Perfect Works Build %hs found in %s: %s"), Found.PerfectWorks->Version().c_str(), *ModsFolder(),
			InUse.bEnabled ? TEXT("in use") : TEXT("not in use"));
		if (!Found.PerfectWorks->KnowsBattleEventCode())
		{
			UE_LOG(LogWeltall, Warning, TEXT("Perfect Works Build %hs changes the battle dialogue's windows in a way Weltall doesn't know: the battles keep the original dialogue"),
				Found.PerfectWorks->Version().c_str());
		}
		if (InUse.bRebalancedParty && !Found.PerfectWorks->KnowsRebalanceCode())
		{
			UE_LOG(LogWeltall, Warning, TEXT("Perfect Works Build %hs rebalances the party and items with code Weltall doesn't know: the game keeps the original party, items and rules"),
				Found.PerfectWorks->Version().c_str());
		}
		if (InUse.bRebalancedEnemies && !Found.PerfectWorks->KnowsEnemiesCode())
		{
			UE_LOG(LogWeltall, Warning, TEXT("Perfect Works Build %hs rebalances the enemies with code Weltall doesn't know: the game keeps the original enemies"),
				Found.PerfectWorks->Version().c_str());
		}
		if (InUse.Arena != Weltall::EPerfectWorksArena::Original && !Found.PerfectWorks->KnowsArenaCode(InUse.Arena, InUse.bScript))
		{
			UE_LOG(LogWeltall, Warning, TEXT("Perfect Works Build %hs rebalances the Battling arena with code Weltall doesn't know: the arena keeps its original gears and rules"),
				Found.PerfectWorks->Version().c_str());
		}
		if (Found.PerfectWorks->UsesRebalancedParty(InUse))
		{
			UE_LOG(LogWeltall, Log, TEXT("Perfect Works Build's rebalanced party and items in use, with its rules"));
		}
		if (Found.PerfectWorks->UsesArena(InUse) != Weltall::EPerfectWorksArena::Original)
		{
			UE_LOG(LogWeltall, Log, TEXT("Perfect Works Build's arena (%s mode) in use, with its rules"),
				Found.PerfectWorks->UsesArena(InUse) == Weltall::EPerfectWorksArena::Expert ? TEXT("Expert") : TEXT("Basic"));
		}
	}
	else
	{
		InUse.bEnabled = false;
	}
	Refresh();
}

void UGameModsSubsystem::Refresh()
{
	const Weltall::FGameLibrary& Library = GetGameInstance()->GetSubsystem<UGameDiscsSubsystem>()->Library();
	if (const std::optional<Weltall::FError> Error = Weltall::ApplyMods(Library, Found, InUse))
	{
		UE_LOG(LogWeltall, Warning, TEXT("The mods' files are not used: %hs"), Error->Message.c_str());
	}
}

Weltall::FMenuMods UGameModsSubsystem::MenuMods() const
{
	return {Found.PerfectWorks.has_value(), Found.PerfectWorks ? Found.PerfectWorks->Version() : std::string(), InUse};
}
