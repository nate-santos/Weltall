#include "Shell/BundledDiscsPreparation.h"

#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Shell/PlayerFolders.h"
#include "WeltallLog.h"

#include <atomic>
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

	std::filesystem::path NativePath(const FString& Path)
	{
		return std::filesystem::u8path(TCHAR_TO_UTF8(*Path));
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
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("/"), false);
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..")) return false;
		}
		return !Path.Contains(TEXT("\t")) && !Path.Contains(TEXT("\r")) && !Path.Contains(TEXT("\n"));
	}

	bool ParseManifest(const FString& Text, TArray<FBundledFile>& Files, uint64& Total)
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
			Files.Add({Fields[2], Fields[0].ToLower(), Size});
			Total += Size;
		}
		return !Files.IsEmpty();
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

	bool Prepare(const TArray<FBundledFile>& Files, const FString& Destination, const FString& Version,
		const TSharedRef<FBundledDiscsProgress, ESPMode::ThreadSafe>& Progress)
	{
		std::error_code Error;
		std::filesystem::create_directories(NativePath(Destination), Error);
		if (Error) return Progress->Fail(TEXT("The phone could not create game storage."));
		const FString Stamp = FPaths::GetPath(Destination) / TEXT("Ready.txt");
		bool bReady = NativeMarkerMatches(Stamp, Version);
		for (const FBundledFile& File : Files) bReady &= NativeFileMatches(Destination / File.Relative, File.Size);
		if (bReady)
		{
			Progress->Done = Progress->Total;
			return true;
		}
		// On an interrupted launch, release abandoned partial copies before measuring free space.
		// A verified completed file can be reused; it need not occupy another full copy's space.
		TArray<FBundledFile> Pending;
		uint64 Required = 0;
		for (const FBundledFile& File : Files)
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
		if (!FPlatformMisc::GetDiskTotalAndFreeSpace(Destination, TotalBytes, FreeBytes)
			|| FreeBytes < Required + 256ull * 1024 * 1024)
		{
			return Progress->Fail(TEXT("There is not enough free phone storage to prepare both discs. Free space and restart the app."));
		}
		IPlatformFile& PakFiles = FPlatformFileManager::Get().GetPlatformFile();
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(1024 * 1024);
		for (const FBundledFile& File : Pending)
		{
			if (Progress->Cancelled.load()) return false;
			const FString Target = Destination / File.Relative;
			const FString Partial = Target + TEXT(".partial");
			std::filesystem::create_directories(NativePath(FPaths::GetPath(Target)), Error);
			if (Error) return Progress->Fail(TEXT("The phone could not create game storage."));
			TUniquePtr<IFileHandle> Source(PakFiles.OpenRead(*(BundleSource() / File.Relative)));
			std::ofstream Output(NativePath(Partial), std::ios::binary | std::ios::trunc);
			if (!Source || Source->Size() < 0 || static_cast<uint64>(Source->Size()) != File.Size || !Output)
			{
				return Progress->Fail(TEXT("Bundled game files could not be opened. Reinstall the local APK."));
			}
			FSHA1 Digest;
			uint64 Remaining = File.Size;
			while (Remaining)
			{
				if (Progress->Cancelled.load()) return false;
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
	TArray<FBundledFile> Files;
	if (!ParseManifest(Text, Files, Progress->Total))
	{
		bFailed = true;
		Progress->Fail(TEXT("Bundled game information is invalid. Rebuild or reinstall the local APK."));
		return;
	}
	const FString Destination = GamesFolder();
	const FString Version = HashText(Text);
	Task = Async(EAsyncExecution::ThreadPool, [Files = MoveTemp(Files), Destination, Version, Shared = Progress.ToSharedRef()]()
	{
		return Prepare(Files, Destination, Version, Shared);
	});
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
	return FText::FromString(FString::Printf(TEXT("Preparing both game discs: %llu%%\n\nThis happens on the first launch. Keep the app open."), Percent));
}
