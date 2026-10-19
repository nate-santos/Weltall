#include "ExportTextCommandlet.h"

#include "Game/GameTextExport.h"
#include "HAL/PlatformTime.h"
#include "Media/GameFileIndex.h"
#include "Media/GameLibrary.h"
#include "Media/PsxExecutable.h"
#include "Misc/Paths.h"
#include "Shell/TextModFiles.h"

DEFINE_LOG_CATEGORY_STATIC(LogExportText, Log, All);

UExportTextCommandlet::UExportTextCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UExportTextCommandlet::Main(const FString& Params)
{
	FString Out;
	FParse::Value(*Params, TEXT("Out="), Out);
	if (Out.IsEmpty())
	{
		UE_LOG(LogExportText, Error, TEXT("Usage: -run=ExportText -Out=<assets folder>"));
		return 1;
	}
	Out = FPaths::ConvertRelativePathToFull(Out);
	const Weltall::FGameLibrary Library = Weltall::FGameLibrary::Scan(std::filesystem::path(*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Games"))));
	const Weltall::FIso9660* Disc = Library.Find(Weltall::EDiscRelease::UsaDisc1);
	const auto Executable = Disc != nullptr ? Weltall::ReadBootExecutable(*Disc) : Weltall::TResult<Weltall::FPsxExecutable>(Weltall::MakeError("Disc 1 is not in the Games folder"));
	const auto Index = Executable ? Weltall::FGameFileIndex::Read(*Executable) : Weltall::TResult<Weltall::FGameFileIndex>(Executable.Error());
	if (!Index)
	{
		UE_LOG(LogExportText, Error, TEXT("%hs"), Index.Error().Message.c_str());
		return 1;
	}
	const FString FontFolder = FPaths::Combine(Out, TEXT("font"));
	auto Export = Weltall::FGameTextExport::Open(*Disc, *Index, std::filesystem::path(*FPaths::Combine(Out, TEXT("text"))), std::filesystem::path(*FontFolder));
	if (!Export)
	{
		UE_LOG(LogExportText, Error, TEXT("%hs"), Export.Error().Message.c_str());
		return 1;
	}
	const double Started = FPlatformTime::Seconds();
	for (;;)
	{
		const Weltall::TResult<bool> Done = (*Export)->Step();
		if (!Done)
		{
			UE_LOG(LogExportText, Error, TEXT("%hs"), Done.Error().Message.c_str());
			return 1;
		}
		if (*Done)
		{
			break;
		}
	}
	SaveFontSheetPicture(FontFolder, (*Export)->FontPicture());
	UE_LOG(LogExportText, Display, TEXT("Exported %d files in %.1f s"), (*Export)->TotalFiles(), FPlatformTime::Seconds() - Started);
	return 0;
}
