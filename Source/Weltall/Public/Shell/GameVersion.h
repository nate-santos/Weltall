#pragma once

#include "Containers/UnrealString.h"

/**
 * The game's version as the project's settings name it ("0.8.0"), with the commit a package was built from after it
 * ("0.8.0 (02cac3c)") where the package's Revision.txt beside the game says.
 */
WELTALL_API FString GameVersion();

/** The game's version as the project's settings name it ("0.8.0"). */
WELTALL_API FString ProjectVersion();
