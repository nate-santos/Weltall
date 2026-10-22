#include "Shell/GameVersion.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString ProjectVersion()
{
	FString Version;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
	return Version;
}

FString GameVersion()
{
	FString Version = ProjectVersion();
	FString Revision;
	if (
#if PLATFORM_ANDROID
		FFileHelper::LoadFileToString(Revision, *(FPaths::ProjectContentDir() / TEXT("AndroidBuild/Revision.txt"))) ||
#endif
		FFileHelper::LoadFileToString(Revision, *FPaths::Combine(FPaths::RootDir(), TEXT("Revision.txt"))))
	{
		Revision.TrimStartAndEndInline();
		if (!Revision.IsEmpty())
		{
			Version += FString::Printf(TEXT(" (%s)"), *Revision);
		}
	}
	return Version;
}
