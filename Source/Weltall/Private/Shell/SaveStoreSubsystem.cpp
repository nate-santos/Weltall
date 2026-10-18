#include "Shell/SaveStoreSubsystem.h"

#include "Shell/PlayerFolders.h"
#include "Shell/AndroidSaveDocuments.h"

void USaveStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Saves.emplace(std::filesystem::path(*PlayerSavesFolder()));
#if PLATFORM_ANDROID
	Saves->SetDocuments(MakeAndroidSaveDocuments());
#endif
}
