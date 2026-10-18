#include "Shell/StoryDocuments.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

Weltall::TResult<Weltall::FStoryPacks> ReadStoryDocuments()
{
	const FString Root = FPaths::ProjectContentDir() / TEXT("Story");
#if PLATFORM_ANDROID
	// APK/pak entries are virtual files. Feed decoded text directly to the existing parser.
	Weltall::FStoryPacks Result;
	TArray<FString> Packs;
	IFileManager::Get().FindFiles(Packs, *(Root / TEXT("*")), false, true);
	Packs.Sort();
	for (const FString& Pack : Packs)
	{
		TArray<FString> Documents;
		IFileManager::Get().FindFiles(Documents, *(Root / Pack / TEXT("*.txt")), true, false);
		Documents.Sort();
		for (const FString& Document : Documents)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Root / Pack / Document)))
			{
				return Weltall::MakeError(std::string(TCHAR_TO_UTF8(*(Pack / Document))) + " cannot be read");
			}
			if (const auto Error = Result.Add(TCHAR_TO_UTF8(*Pack), TCHAR_TO_UTF8(*Document), TCHAR_TO_UTF8(*Text)))
			{
				return *Error;
			}
		}
	}
	return Result;
#else
	return Weltall::FStoryPacks::Read(std::filesystem::path(*FPaths::ConvertRelativePathToFull(Root)));
#endif
}
