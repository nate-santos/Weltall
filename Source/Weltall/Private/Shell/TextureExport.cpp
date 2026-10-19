#include "Shell/TextureExport.h"

#include "Async/ParallelFor.h"
#include "Game/DiscTextureCatalogue.h"
#include "Graphics/TextureRegion.h"
#include "Graphics/TextureSheet.h"
#include "HAL/FileManager.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rendering/TextureModLibrary.h"
#include "Rendering/TexturePngFiles.h"
#include "WeltallLog.h"

#include <atomic>

const TCHAR* FTextureExport::IndexName = TEXT("index.tsv");

FTextureExport::FTextureExport(const Weltall::FIso9660& InDisc, const Weltall::FGameFileIndex& InIndex, const FString& TexturesFolder)
	: Disc(InDisc), Index(InIndex), Originals(TexturesFolder / FTextureModLibrary::OriginalsFolder)
{
	Disc.Image().UseDiscAlone();
	Catalogue = std::make_unique<Weltall::FDiscTextureCatalogue>(Disc.Image(), Index);
	Wrappers = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
}

FTextureExport::~FTextureExport() = default;

Weltall::TResult<TUniquePtr<FTextureExport>> FTextureExport::Open(const Weltall::FIso9660& Disc, const Weltall::FGameFileIndex& Index,
	const FString& TexturesFolder)
{
	if (!IFileManager::Get().MakeDirectory(*(TexturesFolder / FTextureModLibrary::OriginalsFolder), true))
	{
		return Weltall::MakeError("Cannot create the texture export's folder.");
	}
	TUniquePtr<FTextureExport> Export(new FTextureExport(Disc, Index, TexturesFolder));
	Export->Root = CanonicalTextureRoot(TexturesFolder);
	if (Export->Root.empty())
	{
		return Weltall::MakeError("Cannot resolve the texture folder.");
	}
	return Export;
}

uint32 FTextureExport::CompletedItems() const { return static_cast<uint32>(Catalogue->CompletedItems()); }
uint32 FTextureExport::TotalItems() const { return static_cast<uint32>(Catalogue->TotalItems()); }

Weltall::TResult<bool> FTextureExport::Step()
{
	if (bDone) return true;
	if (Catalogue->IsComplete())
	{
		if (!WriteIndex()) return Weltall::MakeError("Cannot write the texture index.");
		bDone = true;
		return true;
	}
	const std::size_t Item = Catalogue->CompletedItems();
	const std::size_t PlacesBefore = Catalogue->Places().size();
	const auto Pictures = Catalogue->Step();
	if (!Pictures)
	{
		UE_LOG(LogWeltall, Warning, TEXT("Texture export passes over %hs"), Pictures.Error().Message.c_str());
		return false;
	}
	// Most of the disc's files hold no picture: they get no folder.
	if (Pictures->empty() && Catalogue->Places().size() == PlacesBefore) return false;
	const FString Folder = FolderOf(Item);
	if (!IFileManager::Get().MakeDirectory(*Folder, true) || !IsSafeTexturePath(Folder / TEXT("."), Root))
	{
		return Weltall::MakeError("Cannot use the texture folder of " + Catalogue->FolderOf(Item) + ".");
	}
	TArray<const Weltall::FDiscTexture*> Alone;
	TMap<FString, TArray<Weltall::FTextureSheetPicture>> Sheets;
	for (const Weltall::FDiscTexture& Picture : *Pictures)
	{
		if (Picture.Sheet.empty()) Alone.Add(&Picture);
		else Sheets.FindOrAdd(UTF8_TO_TCHAR(Picture.Sheet.c_str())).Add({Picture.Key, &Picture.Image});
	}
	std::atomic<bool> bFailed = false;
	ParallelFor(Alone.Num(), [this, &Alone, &Folder, &bFailed](int32 Number)
	{
		const Weltall::FDiscTexture& Picture = *Alone[Number];
		const FString File = Folder / PictureName(Picture.Key);
		if (IFileManager::Get().FileExists(*File)) return;
		if (!WriteTexturePng(*Wrappers, File, Root, Picture.Image.Pixels.data(), Picture.Image.Width, Picture.Image.Height, true)) bFailed = true;
	});
	for (const auto& [Name, Cells] : Sheets)
	{
		if (!WriteSheet(Folder / Name, Cells)) bFailed = true;
	}
	if (bFailed) return Weltall::MakeError("Cannot write the textures of " + Catalogue->FolderOf(Item) + ".");
	for (const Weltall::FDiscTexture* Picture : Alone) FirstItems.Add(Picture->Key, static_cast<uint32>(Item));
	// A picture an earlier item saved is linked into this item's folder too, where the disk can: each folder then holds every picture its item draws.
	// A link is a new name in the folder checked above.
	for (std::size_t Place = PlacesBefore; Place < Catalogue->Places().size(); ++Place)
	{
		const uint64 Key = Catalogue->Places()[Place].Key;
		const uint32* First = FirstItems.Find(Key);
		if (First == nullptr || *First == Item) continue;
		std::error_code Error;
		std::filesystem::create_hard_link(std::filesystem::path(*(FolderOf(*First) / PictureName(Key))), std::filesystem::path(*(Folder / PictureName(Key))), Error);
	}
	return false;
}

bool FTextureExport::WriteSheet(const FString& Path, TConstArrayView<Weltall::FTextureSheetPicture> Cells) const
{
	const FString Png = Path + TEXT(".png");
	if (IFileManager::Get().FileExists(*Png)) return true;
	const std::span<const Weltall::FTextureSheetPicture> Pictures(Cells.GetData(), static_cast<std::size_t>(Cells.Num()));
	const Weltall::FTextureSheetLayout Layout = Weltall::LayOutTextureSheet(Pictures);
	const Weltall::FImageRgba8 Sheet = Weltall::DrawTextureSheet(Layout, Pictures);
	const FString Text = UTF8_TO_TCHAR(Weltall::WriteTextureSheetLayout(Layout).c_str());
	const FString Tsv = Path + TEXT(".tsv");
	std::error_code Error;
	if (std::filesystem::is_symlink(std::filesystem::symlink_status(std::filesystem::path(*Tsv), Error))) return false;
	return FFileHelper::SaveStringToFile(Text, *Tsv, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
		WriteTexturePng(*Wrappers, Png, Root, Sheet.Pixels.data(), Sheet.Width, Sheet.Height, true);
}

FString FTextureExport::FolderOf(std::size_t Item) const
{
	return Originals / UTF8_TO_TCHAR(Catalogue->FolderOf(Item).c_str());
}

FString FTextureExport::PictureName(uint64 Key)
{
	return FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(Key).c_str())) + TEXT(".png");
}

bool FTextureExport::WriteIndex() const
{
	FString Text = TEXT("key\twidth\theight\tfolder\n");
	for (const Weltall::FDiscTexturePlace& Place : Catalogue->Places())
	{
		Text += FString::Printf(TEXT("%hs\t%d\t%d\t%hs\n"), Weltall::AssetKeyName(Place.Key).c_str(), Place.Width, Place.Height,
			Catalogue->FolderOf(Place.Item).c_str());
	}
	const FString File = Originals / IndexName;
	const FString Partial = File + TEXT(".partial");
	if (!IsSafeTexturePath(File, Root) || !IsSafeTexturePath(Partial, Root)) return false;
	return FFileHelper::SaveStringToFile(Text, *Partial, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*File, *Partial, true);
}
