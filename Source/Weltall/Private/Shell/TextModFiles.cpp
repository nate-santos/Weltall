#include "Shell/TextModFiles.h"

#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rendering/TexturePngFiles.h"
#include "WeltallLog.h"

namespace
{
	/** The largest font sheet picture read: the grid at sixteen times its size, compressed. */
	constexpr int64 MostFontPictureBytes = 64LL * 1024 * 1024;
}

std::shared_ptr<const Weltall::FMesFontSheet> FindFontSheet(const FString& Folder)
{
	const FString Table = FPaths::Combine(Folder, TEXT("font.tsv"));
	const FString Picture = FPaths::Combine(Folder, TEXT("font.png"));
	const bool bTable = IFileManager::Get().FileExists(*Table);
	const bool bPicture = IFileManager::Get().FileExists(*Picture);
	if (!bTable && !bPicture)
	{
		return nullptr;
	}
	TArray<uint8> TableBytes;
	if (bTable && !FFileHelper::LoadFileToArray(TableBytes, *Table))
	{
		UE_LOG(LogWeltall, Warning, TEXT("Font sheet: cannot read %s"), *Table);
		return nullptr;
	}
	Weltall::FRgbaPicture Pixels;
	if (bPicture)
	{
		TArray<uint8> Compressed;
		TArray<uint8> Raw;
		IImageWrapperModule& ImageWrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = ImageWrappers.CreateImageWrapper(EImageFormat::PNG);
		if (IFileManager::Get().FileSize(*Picture) > MostFontPictureBytes || !FFileHelper::LoadFileToArray(Compressed, *Picture) || !Png
			|| !Png->SetCompressed(Compressed.GetData(), Compressed.Num()) || !Png->GetRaw(ERGBFormat::RGBA, 8, Raw))
		{
			UE_LOG(LogWeltall, Warning, TEXT("Font sheet: cannot read %s"), *Picture);
			return nullptr;
		}
		Pixels = {static_cast<int32>(Png->GetWidth()), static_cast<int32>(Png->GetHeight()), std::vector<uint8>(Raw.GetData(), Raw.GetData() + Raw.Num())};
	}
	const auto Report = [](const std::string& Problem) { UE_LOG(LogWeltall, Warning, TEXT("Font sheet: %s"), UTF8_TO_TCHAR(Problem.c_str())); };
	const std::string_view TableText(reinterpret_cast<const char*>(TableBytes.GetData()), static_cast<std::size_t>(TableBytes.Num()));
	auto Read = Weltall::FMesFontSheet::Read(TableText, bPicture ? &Pixels : nullptr, Report);
	if (!Read)
	{
		UE_LOG(LogWeltall, Warning, TEXT("Font sheet: %hs"), Read.Error().Message.c_str());
		return nullptr;
	}
	UE_LOG(LogWeltall, Log, TEXT("Font sheet found: %d glyphs drawn, %dx"), static_cast<int32>((*Read)->Glyphs().size()), (*Read)->Scale());
	return *Read;
}

std::shared_ptr<const Weltall::FMessageOverrides> FindMessageOverrides(const std::filesystem::path& Folder, const Weltall::FMesFontSheet* Sheet)
{
	const auto Report = [](const std::string& Problem) { UE_LOG(LogWeltall, Warning, TEXT("Text overrides: %s"), UTF8_TO_TCHAR(Problem.c_str())); };
	auto Found = Weltall::FMessageOverrides::Read(Folder, Sheet != nullptr ? Sheet->Characters() : Weltall::FMesCharacters::Font(), Report,
		Sheet != nullptr ? Sheet->Widths() : Weltall::FMesGlyphWidths{});
	if (!Found)
	{
		UE_LOG(LogWeltall, Warning, TEXT("Text overrides: %hs"), Found.Error().Message.c_str());
		return nullptr;
	}
	if ((*Found)->Count() != 0)
	{
		UE_LOG(LogWeltall, Log, TEXT("Text overrides found: %d messages"), static_cast<int32>((*Found)->Count()));
	}
	return *Found;
}

void SaveFontSheetPicture(const FString& Folder, const Weltall::FRgbaPicture& Picture)
{
	const FString File = FPaths::Combine(Folder, TEXT("original"), TEXT("font.png"));
	IImageWrapperModule& ImageWrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (!WriteTexturePng(ImageWrappers, File, CanonicalTextureRoot(Folder), Picture.Pixels.data(), Picture.Width, Picture.Height))
	{
		UE_LOG(LogWeltall, Warning, TEXT("Font sheet: cannot save %s"), *File);
	}
}
