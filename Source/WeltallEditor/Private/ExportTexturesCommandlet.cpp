#include "ExportTexturesCommandlet.h"

#include "HAL/PlatformTime.h"
#include "Media/GameLibrary.h"
#include "Shell/GameDiscsSubsystem.h"
#include "Media/PsxExecutable.h"
#include "Misc/Paths.h"
#include "Shell/TextureExport.h"

DEFINE_LOG_CATEGORY_STATIC(LogExportTextures, Log, All);

UExportTexturesCommandlet::UExportTexturesCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UExportTexturesCommandlet::Main(const FString& Params)
{
	FString Out;
	FParse::Value(*Params, TEXT("Out="), Out);
	if (Out.IsEmpty())
	{
		UE_LOG(LogExportTextures, Error, TEXT("Usage: -run=ExportTextures -Out=<textures folder>"));
		return 1;
	}
	const Weltall::FGameLibrary Library = Weltall::FGameLibrary::Scan(std::filesystem::path(*UGameDiscsSubsystem::GamesFolder()));
	const Weltall::FIso9660* Disc = Library.Find(Weltall::EDiscRelease::UsaDisc1);
	const auto Executable = Disc != nullptr ? Weltall::ReadBootExecutable(*Disc) : Weltall::TResult<Weltall::FPsxExecutable>(Weltall::MakeError("Disc 1 is not in the Games folder"));
	const auto Index = Executable ? Weltall::FGameFileIndex::Read(*Executable) : Weltall::TResult<Weltall::FGameFileIndex>(Executable.Error());
	if (!Index)
	{
		UE_LOG(LogExportTextures, Error, TEXT("%hs"), Index.Error().Message.c_str());
		return 1;
	}
	auto Export = FTextureExport::Open(*Disc, *Index, FPaths::ConvertRelativePathToFull(Out));
	if (!Export)
	{
		UE_LOG(LogExportTextures, Error, TEXT("%hs"), Export.Error().Message.c_str());
		return 1;
	}
	const double Started = FPlatformTime::Seconds();
	for (;;)
	{
		const Weltall::TResult<bool> Done = (*Export)->Step();
		if (!Done)
		{
			UE_LOG(LogExportTextures, Error, TEXT("%hs"), Done.Error().Message.c_str());
			return 1;
		}
		if (*Done)
		{
			break;
		}
	}
	UE_LOG(LogExportTextures, Display, TEXT("Exported %u items in %.1f s"), (*Export)->TotalItems(), FPlatformTime::Seconds() - Started);
	return 0;
}
