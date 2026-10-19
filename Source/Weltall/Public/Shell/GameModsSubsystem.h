#pragma once

#include "CoreMinimal.h"
#include "Menu/MenuProgram.h"
#include "Mods/GameMods.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameModsSubsystem.generated.h"

/**
 * The player's mods, found in the Mods folder beside Games as the game starts, their files given to the discs as the
 * settings chose then: a change in the options applies from the next start.
 */
UCLASS()
class WELTALL_API UGameModsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static FString ModsFolder();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Every disc found so far given the mods' files the game started with: after the Games folder is looked in again. */
	void Refresh();

	/** What the options screen shows of the mods. */
	Weltall::FMenuMods MenuMods() const;

private:
	Weltall::FFoundMods Found;
	Weltall::FPerfectWorksOptions InUse;
};
