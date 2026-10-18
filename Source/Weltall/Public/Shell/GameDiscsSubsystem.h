#pragma once

#include "CoreMinimal.h"
#include "Media/GameLibrary.h"
#include "Shell/BundledDiscsPreparation.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameDiscsSubsystem.generated.h"

/** Disc discovery, including preparation of locally bundled Android disc files. */
UCLASS()
class WELTALL_API UGameDiscsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Physical disc directory: Android app-private storage, desktop Games beside the game, or the -Games= override. */
	static FString GamesFolder();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Progress/error for first-run local bundled data; empty when normal discovery is ready. */
	FText PreparationNotice() const { return Preparation.Notice(); }

	/** The disc of a release, or null when the folder has none. */
	const Weltall::FIso9660* Find(Weltall::EDiscRelease Release) const;

	/** Looks in the folder again: discs not found before are added, the ones found stay (the game may be reading them). */
	void Rescan();

	/** What was found, and the notes on the rest. */
	const Weltall::FGameLibrary& Library() const { return Discs; }

private:
	void LogNotes() const;

	Weltall::FGameLibrary Discs;
	FBundledDiscsPreparation Preparation;
};
