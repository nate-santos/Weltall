#include "Shell/BundledDiscsPreparation.h"

#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Shell/PlayerFolders.h"
#include "Shell/PlatformByteSource.h"
#include "Media/CueSheet.h"
#include "Foundation/FileByteSource.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "WeltallLog.h"

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

struct FBundledDiscsProgress
{
	std::atomic<uint64> Done{0};
	uint64 Total = 0;
	std::atomic<bool> Cancelled{false};
	FCriticalSection Mutex;
	FString Error;
	Weltall::FGameLibrary Library;

	bool Fail(const FString& Message)
	{
		FScopeLock Lock(&Mutex);
		Error = Message;
		UE_LOG(LogWeltall, Error, TEXT("Bundled disc preparation: %s"), *Message);
		return false;
	}
};

namespace
{
	struct FBundledFile
	{
		FString Relative;
		FString Hash;
		uint64 Size = 0;
	};

	/** The worker's platform responses; tests supply bounded storage/cancellation without filling a device. */
	struct FPreparationServices
	{
		TFunction<bool(const FString&, uint64&)> FreeSpace;
		TFunction<bool(uint64)> Cancelled;
	};

	std::filesystem::path NativePath(const FString& Path)
	{
#if PLATFORM_WINDOWS
		return std::filesystem::path(*Path);
#else
		// Android/POSIX filenames use UTF-8; u8path is deprecated in C++20.
		return std::filesystem::path(TCHAR_TO_UTF8(*Path));
#endif
	}

	FString BundleSource()
	{
		return FPaths::ProjectContentDir() / TEXT("BundledDiscs");
	}

	FString Manifest()
	{
		FString Text;
#if PLATFORM_ANDROID
		FFileHelper::LoadFileToString(Text, *(BundleSource() / TEXT("Manifest.txt")));
#endif
		return Text;
	}

	FString HashText(const FString& Text)
	{
		FTCHARToUTF8 Utf8(*Text);
		return FSHA1::HashBuffer(Utf8.Get(), Utf8.Length()).ToString().ToLower();
	}

	bool SafeRelative(const FString& Path)
	{
		if (Path.IsEmpty() || !FPaths::IsRelative(Path) || Path.Contains(TEXT("\\")) || Path.Contains(TEXT(":"))) return false;
		for (const TCHAR Character : Path) if (Character < TEXT(' ')) return false;
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), false);
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..")) return false;
		}
		return true;
	}

	bool ParseManifest(const FString& Text, TArray<FBundledFile>& BundledFiles, uint64& Total)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		if (Lines.Num() < 2 || Lines[0] != TEXT("WELTALL_BUNDLED_DISCS_V1")) return false;
		TSet<FString> Names;
		for (int32 Index = 1; Index < Lines.Num(); ++Index)
		{
			TArray<FString> Fields;
			Lines[Index].ParseIntoArray(Fields, TEXT("|"), false);
			if (Fields.Num() != 3 || Fields[0].Len() != 40 || !SafeRelative(Fields[2])) return false;
			for (const TCHAR Character : Fields[0]) if (!FChar::IsHexDigit(Character)) return false;
			if (Fields[1].IsEmpty()) return false;
			for (const TCHAR Character : Fields[1]) if (!FChar::IsDigit(Character)) return false;
			const uint64 Size = FCString::Strtoui64(*Fields[1], nullptr, 10);
			if (Size == 0 || Size > 4ull * 1024 * 1024 * 1024 || Total > MAX_uint64 - Size) return false;
			const FString Name = Fields[2].ToLower();
			if (Names.Contains(Name)) return false;
			Names.Add(Name);
			BundledFiles.Add({Fields[2], Fields[0].ToLower(), Size});
			Total += Size;
		}
		return !BundledFiles.IsEmpty();
	}

	bool NativeFileMatches(const FString& Path, uint64 Size)
	{
		std::error_code Error;
		const auto Actual = std::filesystem::file_size(NativePath(Path), Error);
		return !Error && Actual == Size;
	}

	bool NativeMarkerMatches(const FString& Path, const FString& Value)
	{
		std::ifstream Input(NativePath(Path), std::ios::binary);
		std::string Line;
		std::getline(Input, Line);
		return Line == TCHAR_TO_UTF8(*Value);
	}

	bool WriteMarker(const FString& Path, const FString& Value)
	{
		std::ofstream Output(NativePath(Path + TEXT(".partial")), std::ios::binary | std::ios::trunc);
		Output << TCHAR_TO_UTF8(*Value) << '\n';
		Output.close();
		if (!Output) return false;
		std::error_code Error;
		std::filesystem::rename(NativePath(Path + TEXT(".partial")), NativePath(Path), Error);
		return !Error;
	}

	/** No component below the app-owned root may redirect cleanup elsewhere. Missing files are safe to retry. */
	bool SafeOwnedPath(const FString& Owner, const FString& Target)
	{
		const auto Root = NativePath(Owner).lexically_normal();
		const auto Path = NativePath(Target).lexically_normal();
		const auto Relative = Path.lexically_relative(Root);
		if (Relative.empty() || Relative.is_absolute()) return false;
		for (const auto& Part : Relative) if (Part == ".." || Part == ".") return false;
		std::error_code Error;
		auto Current = Root;
		if (std::filesystem::is_symlink(std::filesystem::symlink_status(Current, Error))) return false;
		if (Error && Error != std::errc::no_such_file_or_directory) return false;
		for (const auto& Part : Relative)
		{
			Current /= Part;
			Error.clear();
			if (std::filesystem::is_symlink(std::filesystem::symlink_status(Current, Error))) return false;
			if (Error && Error != std::errc::no_such_file_or_directory) return false;
		}
		return true;
	}

	/** Removes only byte-verified old app copies; never removes directories or unknown neighboring files. */
	bool ReclaimExtracted(const TArray<FBundledFile>& Entries, const FString& Destination, const FString& Owner,
		const FString& Version, const TSharedRef<FBundledDiscsProgress, ESPMode::ThreadSafe>& Progress)
	{
		TArray<FString> Remove;
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(1024 * 1024);
		uint64 Bytes = 0;
		for (const FBundledFile& Entry : Entries)
		{
			const FString Path = Destination / Entry.Relative;
			if (!SafeOwnedPath(Owner, Path) || !SafeOwnedPath(Owner, Path + TEXT(".verified"))) return false;
			std::error_code Error;
			const bool bExists = std::filesystem::exists(NativePath(Path), Error);
			if (Error) return false;
			const bool bMarker = NativeMarkerMatches(Path + TEXT(".verified"), Entry.Hash);
			if (bExists)
			{
				if (!bMarker || !NativeFileMatches(Path, Entry.Size)) return false;
				auto Source = Weltall::FFileByteSource::Open(NativePath(Path));
				if (!Source) return false;
				FSHA1 Digest;
				for (uint64 Offset = 0; Offset < Entry.Size;)
				{
					if (Progress->Cancelled.load()) return false;
					const uint32 Take = static_cast<uint32>(FMath::Min<uint64>(Buffer.Num(), Entry.Size - Offset));
					if (!(*Source)->Read(Offset, std::span<uint8>(Buffer.GetData(), Take))) return false;
					Digest.Update(Buffer.GetData(), Take);
					Offset += Take;
				}
				Digest.Final();
				uint8 Hash[FSHA1::DigestSize];
				Digest.GetHash(Hash);
				if (BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower() != Entry.Hash) return false;
				Remove.Add(Path);
				Bytes += Entry.Size;
			}
			if (bMarker) Remove.Add(Path + TEXT(".verified"));
		}
		const FString Stamp = FPaths::GetPath(Destination) / TEXT("Ready.txt");
		if (!SafeOwnedPath(Owner, Stamp)) return false;
		if (NativeMarkerMatches(Stamp, Version)) Remove.Add(Stamp);
		for (const FString& Path : Remove)
		{
			if (Progress->Cancelled.load() || !SafeOwnedPath(Owner, Path)) return false;
			std::error_code Error;
			std::filesystem::remove(NativePath(Path), Error);
			if (Error) return false;
		}
		UE_LOG(LogWeltall, Log, TEXT("Reclaimed %llu bytes of verified extracted disc copies; player data untouched"), Bytes);
		return true;
	}

	/** Validate logical bytes once per manifest, then identify both discs through normal core checks. */
	bool OpenPackaged(const TArray<FBundledFile>& Entries, const FString& SourceFolder, const FString& Version,
		const FString& Marker, const TSharedRef<FBundledDiscsProgress, ESPMode::ThreadSafe>& Progress)
	{
		const bool bVerified = NativeMarkerMatches(Marker, Version);
		TMap<FString, std::shared_ptr<const FPlatformByteSource>> Sources;
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(1024 * 1024);
		for (const FBundledFile& Entry : Entries)
		{
			if (Progress->Cancelled) return false;
			auto Source = FPlatformByteSource::Open(SourceFolder / Entry.Relative);
			if (!Source || Source->Size() != Entry.Size) return Progress->Fail(TEXT("A bundled disc is missing or incomplete. Reinstall the APK."));
			if (!bVerified)
			{
				FSHA1 Digest;
				for (uint64 Offset = 0; Offset < Entry.Size;)
				{
					if (Progress->Cancelled) return false;
					const uint32 Take = static_cast<uint32>(FMath::Min<uint64>(Buffer.Num(), Entry.Size - Offset));
					if (!Source->Read(Offset, std::span<uint8>(Buffer.GetData(), Take))) return Progress->Fail(TEXT("A bundled disc cannot be read. Reinstall the APK."));
					Digest.Update(Buffer.GetData(), Take);
					Offset += Take;
					Progress->Done.fetch_add(Take);
				}
				Digest.Final();
				uint8 Hash[FSHA1::DigestSize];
				Digest.GetHash(Hash);
				if (BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower() != Entry.Hash) return Progress->Fail(TEXT("A bundled disc failed verification. Reinstall the APK."));
			}
			Sources.Add(Entry.Relative, std::move(Source));
		}
		int32 Sheets = 0;
		for (const FBundledFile& Entry : Entries)
		{
			if (!Entry.Relative.EndsWith(TEXT(".cue"), ESearchCase::IgnoreCase)) continue;
			++Sheets;
			if (Entry.Size > 1024 * 1024) return Progress->Fail(TEXT("A bundled CUE sheet is too large."));
			TArray<uint8> Text;
			Text.SetNumUninitialized(static_cast<int32>(Entry.Size) + 1);
			if (!Sources[Entry.Relative]->Read(0, std::span<uint8>(Text.GetData(), static_cast<size_t>(Entry.Size)))) return Progress->Fail(TEXT("A bundled CUE sheet cannot be read."));
			Text[static_cast<int32>(Entry.Size)] = 0;
			const auto Tracks = Weltall::ParseCueSheet(std::string_view(reinterpret_cast<const char*>(Text.GetData()), Entry.Size), NativePath(FPaths::GetPath(Entry.Relative)));
			if (!Tracks) return Progress->Fail(TEXT("A bundled CUE sheet is invalid."));
			const auto Track = std::find_if(Tracks->begin(), Tracks->end(), [](const Weltall::FCueTrack& Value) { return !Value.IsAudio(); });
			if (Track == Tracks->end()) return Progress->Fail(TEXT("A bundled disc has no data track."));
			const std::string Relative = Track->File.generic_string();
			const FString Name(UTF8_TO_TCHAR(Relative.c_str()));
			const auto* Source = SafeRelative(Name) ? Sources.Find(Name) : nullptr;
			if (!Source) return Progress->Fail(TEXT("A bundled CUE sheet names a file outside its manifest."));
			auto Image = Weltall::FCdImage::OpenSource(*Source, Track->SectorSize, static_cast<uint64>(Track->StartFrame) * Track->SectorSize);
			Progress->Library.AddImage(NativePath(Entry.Relative), std::move(Image));
		}
		if (Sheets != 2 || !Progress->Library.PlayableDisc(1) || !Progress->Library.PlayableDisc(2))
			return Progress->Fail(TEXT("Both bundled game discs could not be identified."));
		std::error_code Error;
		std::filesystem::create_directories(NativePath(FPaths::GetPath(Marker)), Error);
		if (Error || (!bVerified && !WriteMarker(Marker, Version))) return Progress->Fail(TEXT("Disc verification status could not be saved."));
		Progress->Done = Progress->Total;
		UE_LOG(LogWeltall, Log, TEXT("Both discs opened directly from packaged data; no BIN extraction"));
		return true;
	}

	bool Prepare(const TArray<FBundledFile>& BundledFiles, const FString& SourceFolder, const FString& Destination, const FString& Version,
		const TSharedRef<FBundledDiscsProgress, ESPMode::ThreadSafe>& Progress, const FPreparationServices& Services = {})
	{
		const auto Cancelled = [&]
		{
			return Progress->Cancelled.load() || (Services.Cancelled && Services.Cancelled(Progress->Done.load()));
		};
		std::error_code Error;
		std::filesystem::create_directories(NativePath(Destination), Error);
		if (Error) return Progress->Fail(TEXT("The phone could not create game storage."));
		const FString Stamp = FPaths::GetPath(Destination) / TEXT("Ready.txt");
		bool bReady = NativeMarkerMatches(Stamp, Version);
		for (const FBundledFile& File : BundledFiles) bReady &= NativeFileMatches(Destination / File.Relative, File.Size);
		if (bReady)
		{
			Progress->Done = Progress->Total;
			return true;
		}
		// On an interrupted launch, release abandoned partial copies before measuring free space.
		// A verified completed file can be reused; it need not occupy another full copy's space.
		TArray<FBundledFile> Pending;
		uint64 Required = 0;
		for (const FBundledFile& File : BundledFiles)
		{
			const FString Target = Destination / File.Relative;
			std::filesystem::remove(NativePath(Target + TEXT(".partial")), Error);
			if (Error) return Progress->Fail(TEXT("An interrupted game copy could not be removed. Check phone storage."));
			std::filesystem::remove(NativePath(Target + TEXT(".verified.partial")), Error);
			if (Error) return Progress->Fail(TEXT("An interrupted game copy could not be removed."));
			if (NativeFileMatches(Target, File.Size) && NativeMarkerMatches(Target + TEXT(".verified"), File.Hash))
			{
				Progress->Done.fetch_add(File.Size);
				continue;
			}
			std::filesystem::remove(NativePath(Target), Error);
			if (Error) return Progress->Fail(TEXT("An incomplete game file could not be replaced."));
			std::filesystem::remove(NativePath(Target + TEXT(".verified")), Error);
			if (Error) return Progress->Fail(TEXT("An incomplete game file could not be replaced."));
			Pending.Add(File);
			Required += File.Size;
		}
		uint64 TotalBytes = 0, FreeBytes = 0;
		const bool bSpaceKnown = Services.FreeSpace ? Services.FreeSpace(Destination, FreeBytes)
			: FPlatformMisc::GetDiskTotalAndFreeSpace(Destination, TotalBytes, FreeBytes);
		if (!bSpaceKnown
			|| FreeBytes < Required + 256ull * 1024 * 1024)
		{
			return Progress->Fail(TEXT("There is not enough free phone storage to prepare both discs. Free space and restart the app."));
		}
		IPlatformFile& PakFiles = FPlatformFileManager::Get().GetPlatformFile();
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(1024 * 1024);
		for (const FBundledFile& File : Pending)
		{
			if (Cancelled()) return false;
			const FString Target = Destination / File.Relative;
			const FString Partial = Target + TEXT(".partial");
			std::filesystem::create_directories(NativePath(FPaths::GetPath(Target)), Error);
			if (Error) return Progress->Fail(TEXT("The phone could not create game storage."));
			TUniquePtr<IFileHandle> Source(PakFiles.OpenRead(*(SourceFolder / File.Relative)));
			std::ofstream Output(NativePath(Partial), std::ios::binary | std::ios::trunc);
			if (!Source || Source->Size() < 0 || static_cast<uint64>(Source->Size()) != File.Size || !Output)
			{
				return Progress->Fail(TEXT("Bundled game files could not be opened. Reinstall the local APK."));
			}
			FSHA1 Digest;
			uint64 Remaining = File.Size;
			while (Remaining)
			{
				if (Cancelled()) return false;
				const uint64 Count = FMath::Min<uint64>(Remaining, Buffer.Num());
				if (!Source->Read(Buffer.GetData(), Count)) return Progress->Fail(TEXT("A bundled game file could not be read."));
				Output.write(reinterpret_cast<const char*>(Buffer.GetData()), Count);
				if (!Output) return Progress->Fail(TEXT("A game file could not be written. Check phone storage and restart the app."));
				Digest.Update(Buffer.GetData(), Count);
				Remaining -= Count;
				Progress->Done.fetch_add(Count);
			}
			Output.flush();
			if (!Output || Digest.Finalize().ToString().ToLower() != File.Hash)
			{
				return Progress->Fail(TEXT("Bundled game verification failed. Reinstall the local APK."));
			}
			Output.close();
			// Each complete file replaces its previous copy atomically; .partial is never read as a disc.
			std::filesystem::rename(NativePath(Partial), NativePath(Target), Error);
			if (Error) return Progress->Fail(TEXT("A verified game file could not be put in place."));
			if (!WriteMarker(Target + TEXT(".verified"), File.Hash))
			{
				return Progress->Fail(TEXT("A verified game file could not be recorded. Check phone storage."));
			}
		}
		// Publish readiness only after every disc and descriptor is complete and verified.
		if (!WriteMarker(Stamp, Version)) return Progress->Fail(TEXT("Game preparation could not be completed. Check phone storage."));
		UE_LOG(LogWeltall, Log, TEXT("Both bundled discs are ready in private app storage"));
		return true;
	}
}

FBundledDiscsPreparation::~FBundledDiscsPreparation()
{
	Stop();
}

void FBundledDiscsPreparation::Stop()
{
	if (Progress) Progress->Cancelled = true;
	if (Task.IsValid()) Task.Wait();
	Task = {};
}

FString FBundledDiscsPreparation::GamesFolder()
{
	const FString Text = Manifest();
	return Text.IsEmpty() ? PlayerDataFolder() / TEXT("Games")
		: PlayerDataFolder() / TEXT("BundledDiscs") / HashText(Text) / TEXT("Games");
}

void FBundledDiscsPreparation::Begin()
{
	const FString Text = Manifest();
	if (Text.IsEmpty()) return;
	Progress = MakeShared<FBundledDiscsProgress, ESPMode::ThreadSafe>();
	TArray<FBundledFile> BundledFiles;
	if (!ParseManifest(Text, BundledFiles, Progress->Total))
	{
		bFailed = true;
		Progress->Fail(TEXT("Bundled game information is invalid. Rebuild or reinstall the local APK."));
		return;
	}
	const FString Destination = GamesFolder();
	const FString Version = HashText(Text);
	const FString SourceFolder = BundleSource();
	// Qualified direct reading is the normal Android path. Development comparisons may retain/extract old copies.
	bDirect = true;
#if !UE_BUILD_SHIPPING
	bDirect = !FParse::Param(FCommandLine::Get(), TEXT("WeltallExtractedDiscs"));
#endif
	if (bDirect)
	{
		const FString Marker = PlayerDataFolder() / TEXT("BundledDiscs") / Version / TEXT("DirectVerified.txt");
		const FString Owner = PlayerDataFolder() / TEXT("BundledDiscs");
		bool bReclaim = true;
#if !UE_BUILD_SHIPPING
		bReclaim = !FParse::Param(FCommandLine::Get(), TEXT("WeltallKeepDiscCopies"));
#endif
		Task = Async(EAsyncExecution::ThreadPool, [BundledFiles = MoveTemp(BundledFiles), SourceFolder, Version, Marker, Destination, Owner, bReclaim, Shared = Progress.ToSharedRef()]()
		{
			if (!OpenPackaged(BundledFiles, SourceFolder, Version, Marker, Shared)) return false;
			if (bReclaim && !ReclaimExtracted(BundledFiles, Destination, Owner, Version, Shared))
				UE_LOG(LogWeltall, Warning, TEXT("Disc copies retained: cleanup could not verify every owned file/path"));
			return true;
		});
		return;
	}
	Task = Async(EAsyncExecution::ThreadPool, [BundledFiles = MoveTemp(BundledFiles), SourceFolder, Destination, Version, Shared = Progress.ToSharedRef()]()
	{
		return Prepare(BundledFiles, SourceFolder, Destination, Version, Shared);
	});
}

Weltall::FGameLibrary FBundledDiscsPreparation::TakeLibrary()
{
	return Progress && !IsBlocked() ? std::move(Progress->Library) : Weltall::FGameLibrary{};
}

void FBundledDiscsPreparation::Poll()
{
	if (Task.IsValid() && Task.IsReady())
	{
		bFailed = !Task.Get();
		Task = {};
	}
}

bool FBundledDiscsPreparation::IsBlocked() const
{
	return Task.IsValid() || bFailed;
}

FText FBundledDiscsPreparation::Notice() const
{
	if (!IsBlocked() || !Progress) return FText::GetEmpty();
	if (bFailed)
	{
		FScopeLock Lock(&Progress->Mutex);
		return FText::FromString(Progress->Error);
	}
	const uint64 Percent = Progress->Total ? 100 * Progress->Done.load() / Progress->Total : 0;
	return FText::FromString(bDirect
		? FString::Printf(TEXT("Verifying bundled discs: %llu%%\n\nKeep the app open. The game reads its discs without making another copy."), Percent)
		: FString::Printf(TEXT("Preparing both game discs: %llu%%\n\nThis happens on the first launch. Keep the app open."), Percent));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "BundledDiscsPreparationTests.inl"
#endif
