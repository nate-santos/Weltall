#pragma once

#include "CoreMinimal.h"

/** Physical writable data directory: Android's private app files, or the desktop settings folder. */
WELTALL_API FString PlayerDataFolder();

/** Settings in Android's private files, Windows' local app data, macOS's application support or XDG config home. */
WELTALL_API FString PlayerSettingsFolder();

/** Saves in Android's private files, the desktop settings folder on Windows/macOS or XDG's data home. */
WELTALL_API FString PlayerSavesFolder();
