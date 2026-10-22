// Included with the worker so these generated-data checks exercise its actual parser, copy and publish path.
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

#include <algorithm>
#include <iterator>
#include <array>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPackagedDiscFixtureTest, "Weltall.Storage.BundledDiscs.PackagedReadFixtures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPackagedDiscFixtureTest::RunTest(const FString&)
{
	for (const bool bRaw : {false, true})
	{
		const FString Path = FPaths::ProjectContentDir() / TEXT("StorageFixtures") / (bRaw ? TEXT("Raw.bin") : TEXT("Compressed.bin"));
		auto Source = FPlatformByteSource::Open(Path);
		if (!TestTrue(TEXT("Generated UFS fixture opens"), static_cast<bool>(Source))) return false;
		TArray<uint8> Expected;
		Expected.SetNumUninitialized(3 * 65536 + 17);
		uint32 State = 1;
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			State = 1664525u * State + 1013904223u;
			Expected[Index] = bRaw ? static_cast<uint8>(State >> 24) : static_cast<uint8>(Index % 251);
		}
		TestEqual(TEXT("Mounted fixture size matches"), Source->Size(), static_cast<uint64>(Expected.Num()));
		std::array<uint8, 2352> Data{};
		for (const uint64 Offset : {uint64(0), uint64(65530), uint64(Expected.Num() - Data.size())})
		{
			if (!TestTrue(TEXT("Mounted fixture sector/boundary read"), Source->Read(Offset, Data))) return false;
			TestTrue(TEXT("Mounted bytes unchanged"), FMemory::Memcmp(Data.data(), Expected.GetData() + Offset, Data.size()) == 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlatformDiscReadTest, "Weltall.Storage.BundledDiscs.PlatformReads", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FPlatformDiscReadTest::RunTest(const FString&)
{
	const FString Base = FPaths::ProjectSavedDir() / TEXT("Automation/PlatformReads") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Path = Base / TEXT("fixture.bin");
	IPlatformFile& PlatformFiles = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFiles.CreateDirectoryTree(*Base)) return false;
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(3 * 65536 + 17);
	for (int32 Index = 0; Index < Bytes.Num(); ++Index) Bytes[Index] = static_cast<uint8>(Index % 251);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path)) return false;
	auto Source = FPlatformByteSource::Open(Path);
	if (!TestTrue(TEXT("Source opens through platform-file stack"), static_cast<bool>(Source))) return false;
	TestEqual(TEXT("Exact size"), Source->Size(), static_cast<uint64>(Bytes.Num()));
	TestTrue(TEXT("Empty EOF read"), Source->Read(Source->Size(), {}));
	std::array<uint8, 64> Read{};
	TestFalse(TEXT("Past EOF rejected"), Source->Read(Source->Size(), Read));
	TestFalse(TEXT("Overflow rejected"), Source->Read(MAX_uint64, Read));
	for (uint64 Offset : {uint64(0), uint64(65520), uint64(Bytes.Num() - 64)})
	{
		TestTrue(TEXT("Boundary read succeeds"), Source->Read(Offset, Read));
		TestTrue(TEXT("Bytes match"), FMemory::Memcmp(Read.data(), Bytes.GetData() + Offset, Read.size()) == 0);
	}
	const auto Work = [Source](uint64 Offset)
	{
		std::array<uint8, 2352> Data{};
		for (int32 Attempt = 0; Attempt < 100; ++Attempt)
		{
			if (!Source->Read(Offset, Data)) return false;
			for (size_t Index = 0; Index < Data.size(); ++Index) if (Data[Index] != static_cast<uint8>((Offset + Index) % 251)) return false;
		}
		return true;
	};
	auto A = Async(EAsyncExecution::ThreadPool, [Work] { return Work(65000); });
	auto B = Async(EAsyncExecution::ThreadPool, [Work] { return Work(131000); });
	TestTrue(TEXT("Concurrent seeks do not interfere A"), A.Get());
	TestTrue(TEXT("Concurrent seeks do not interfere B"), B.Get());
	Source.reset();
	TestTrue(TEXT("Reopening succeeds"), static_cast<bool>(FPlatformByteSource::Open(Path)));
	PlatformFiles.DeleteFile(*Path);
	PlatformFiles.DeleteDirectory(*Base);
	return true;
}

namespace
{
	constexpr uint64 BundleStorageReserve = 256ull * 1024 * 1024;
	constexpr EAutomationTestFlags BundleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	struct FBundlePreparationFixture
	{
		FString Base;
		FString Root;
		FString Source;
		FString Games;
		FString Text = TEXT("WELTALL_BUNDLED_DISCS_V1\n");
		TSharedRef<FBundledDiscsProgress, ESPMode::ThreadSafe> Progress = MakeShared<FBundledDiscsProgress, ESPMode::ThreadSafe>();

		FBundlePreparationFixture()
		{
#if PLATFORM_ANDROID
			Base = PlayerDataFolder() / TEXT("Automation/BundledDiscs");
#else
			Base = FPaths::ProjectSavedDir() / TEXT("Automation/BundledDiscs");
#endif
			Base = FPaths::ConvertRelativePathToFull(Base);
			Root = Base / FGuid::NewGuid().ToString(EGuidFormats::Digits);
			Source = Root / TEXT("Source");
			Games = Root / TEXT("Prepared/Games");
		}

		~FBundlePreparationFixture()
		{
			// Only the unique generated fixture below Automation/BundledDiscs may be removed.
			if (!FPaths::IsUnderDirectory(Root, Base) || Root == Base) return;
			std::error_code Error;
			std::filesystem::remove_all(NativePath(Root), Error);
		}

		bool Write(const FString& Path, const TArray<uint8>& Bytes)
		{
			std::error_code Error;
			std::filesystem::create_directories(NativePath(FPaths::GetPath(Path)), Error);
			if (Error) return false;
			std::ofstream File(NativePath(Path), std::ios::binary | std::ios::trunc);
			File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
			File.close();
			return static_cast<bool>(File);
		}

		bool Add(const FString& Name, int32 Size, uint8 Byte)
		{
			TArray<uint8> Bytes;
			Bytes.Init(Byte, Size);
			const FString Hash = FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num()).ToString().ToLower();
			Text += FString::Printf(TEXT("%s|%d|%s\n"), *Hash, Size, *Name);
			return Write(Source / Name, Bytes);
		}

		bool Run(const FPreparationServices& Services = {})
		{
			Progress = MakeShared<FBundledDiscsProgress, ESPMode::ThreadSafe>();
			TArray<FBundledFile> BundleEntries;
			if (!ParseManifest(Text, BundleEntries, Progress->Total)) return Progress->Fail(TEXT("Bundled game information is invalid. Rebuild or reinstall the local APK."));
			return Prepare(BundleEntries, Source, Games, HashText(Text), Progress, Services);
		}

		bool Exists(const FString& Path) const
		{
			std::error_code Error;
			return std::filesystem::exists(NativePath(Path), Error);
		}

		bool Matches(const FString& Path, int32 Size, uint8 Byte) const
		{
			std::ifstream File(NativePath(Path), std::ios::binary);
			const std::string Bytes((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
			return File.is_open() && !File.bad() && Bytes.size() == static_cast<std::size_t>(Size)
				&& std::all_of(Bytes.begin(), Bytes.end(), [Byte](char Value) { return static_cast<uint8>(Value) == Byte; });
		}

		FString Stamp() const { return FPaths::GetPath(Games) / TEXT("Ready.txt"); }
	};

	FPreparationServices BundleSpace(uint64 FreeBytes)
	{
		FPreparationServices Services;
		Services.FreeSpace = [FreeBytes](const FString&, uint64& Bytes) { Bytes = FreeBytes; return true; };
		return Services;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleManifestTest, "Weltall.Storage.BundledDiscs.Manifest", BundleTestFlags)
bool FBundleManifestTest::RunTest(const FString&)
{
	const FString Hash = FString::ChrN(40, TEXT('a'));
	const TArray<FString> Invalid = {
		TEXT(""), TEXT("WELTALL_BUNDLED_DISCS_V1\n"), TEXT("OTHER\n") + Hash + TEXT("|1|data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\nnohash|1|data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|0|data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|-1|data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|4294967297|data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|99999999999999999999999999|data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|../outside.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|/outside.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|C:/outside.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|dir\\data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|dir//data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|./data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|dir/../data.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|data\t.bin\n"),
		TEXT("WELTALL_BUNDLED_DISCS_V1\n") + Hash + TEXT("|1|data.bin\n") + Hash + TEXT("|1|DATA.BIN\n")
	};
	for (const FString& Text : Invalid)
	{
		TArray<FBundledFile> BundleEntries;
		uint64 Total = 0;
		TestFalse(TEXT("Malformed descriptors and unsafe/duplicate paths are refused"), ParseManifest(Text, BundleEntries, Total));
	}
	FString Nul = TEXT("dir/data_.bin");
	// AppendChar(0) is deliberately ignored by FString; replace an existing code unit instead.
	Nul[8] = 0;
	TestEqual(TEXT("The fixture retains the suffix after its embedded NUL"), Nul.Len(), 13);
	TestEqual(TEXT("The fixture really contains a NUL before the suffix"), Nul[8], TCHAR(0));
	TestFalse(TEXT("Embedded NUL cannot alias a different native filename"), SafeRelative(Nul));
	TestTrue(TEXT("Nested names with spaces and Unicode remain supported"), SafeRelative(TEXT("Disc A/Track \u03a9.bin")));
	TArray<FBundledFile> BundleEntries;
	uint64 Total = 0;
	TestTrue(TEXT("Uppercase digests and both nested files parse"), ParseManifest(TEXT("WELTALL_BUNDLED_DISCS_V1\n")
		+ Hash.ToUpper() + TEXT("|17|Disc A/Track.bin\n") + Hash + TEXT("|31|Disc B/Track.bin\n"), BundleEntries, Total));
	TestEqual(TEXT("All file bytes contribute to required space"), Total, uint64(48));
	TestEqual(TEXT("Two distinct tracks survive parsing"), BundleEntries.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleCopyRestartTest, "Weltall.Storage.BundledDiscs.CopyAndRestart", BundleTestFlags)
bool FBundleCopyRestartTest::RunTest(const FString&)
{
	FBundlePreparationFixture Fixture;
	if (!TestTrue(TEXT("Generated sources exist"), Fixture.Add(TEXT("Disc A/Track \u03a9.bin"), 17, 0x29)
		&& Fixture.Add(TEXT("Disc B/Track.bin"), 1031, 0x71))) return false;
	TestTrue(TEXT("Generated bundle copies through the production worker"), Fixture.Run(BundleSpace(BundleStorageReserve + 1048)));
	TestTrue(TEXT("First native file matches generated bytes"), Fixture.Matches(Fixture.Games / TEXT("Disc A/Track \u03a9.bin"), 17, 0x29));
	TestTrue(TEXT("Second native file matches generated bytes"), Fixture.Matches(Fixture.Games / TEXT("Disc B/Track.bin"), 1031, 0x71));
	TestTrue(TEXT("Readiness is published with the manifest version"), NativeMarkerMatches(Fixture.Stamp(), HashText(Fixture.Text)));
	TestFalse(TEXT("No partial file is published as a disc"), Fixture.Exists(Fixture.Games / TEXT("Disc A/Track \u03a9.bin.partial")));
	std::error_code Error;
	std::filesystem::remove(NativePath(Fixture.Source / TEXT("Disc A/Track \u03a9.bin")), Error);
	std::filesystem::remove(NativePath(Fixture.Source / TEXT("Disc B/Track.bin")), Error);
	FPreparationServices Restart;
	Restart.FreeSpace = [this](const FString&, uint64&) { AddError(TEXT("A ready restart must not need a fresh copy/storage query")); return false; };
	TestTrue(TEXT("Restart reuses verified readiness even when source copies are unavailable"), Fixture.Run(Restart));
	TestEqual(TEXT("Reused bytes report complete progress"), Fixture.Progress->Done.load(), uint64(1048));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleSpaceTest, "Weltall.Storage.BundledDiscs.InsufficientStorage", BundleTestFlags)
bool FBundleSpaceTest::RunTest(const FString&)
{
	FBundlePreparationFixture Fixture;
	if (!Fixture.Add(TEXT("Disc A/Track.bin"), 17, 0x19)) return false;
	AddExpectedError(TEXT("not enough free phone storage"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("One byte below the pending bytes plus reserve is refused"), Fixture.Run(BundleSpace(BundleStorageReserve + 16)));
	TestFalse(TEXT("Low space never publishes readiness"), Fixture.Exists(Fixture.Stamp()));
	TestFalse(TEXT("Low space never begins an output copy"), Fixture.Exists(Fixture.Games / TEXT("Disc A/Track.bin.partial")));
	TestTrue(TEXT("Source bytes survive the low-space failure"), Fixture.Matches(Fixture.Source / TEXT("Disc A/Track.bin"), 17, 0x19));
	TestTrue(TEXT("The storage notice explains the recovery action"), Fixture.Progress->Error.Contains(TEXT("Free space and restart")));
	FPreparationServices Unknown;
	Unknown.FreeSpace = [](const FString&, uint64&) { return false; };
	AddExpectedError(TEXT("not enough free phone storage"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("An unavailable space measurement also refuses the copy"), Fixture.Run(Unknown));
	TestTrue(TEXT("Retry at exactly the required budget succeeds"), Fixture.Run(BundleSpace(BundleStorageReserve + 17)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleInterruptedTest, "Weltall.Storage.BundledDiscs.InterruptedCopyRecovery", BundleTestFlags)
bool FBundleInterruptedTest::RunTest(const FString&)
{
	FBundlePreparationFixture Fixture;
	constexpr int32 LargeSize = 2 * 1024 * 1024 + 23;
	if (!Fixture.Add(TEXT("Disc A/Track.bin"), 17, 0x12) || !Fixture.Add(TEXT("Disc B/Track.bin"), LargeSize, 0x75)) return false;
	FPreparationServices Cancel = BundleSpace(MAX_uint64);
	Cancel.Cancelled = [](uint64 Done) { return Done >= 1024 * 1024 + 17; };
	TestFalse(TEXT("Cancellation interrupts an in-flight chunked copy"), Fixture.Run(Cancel));
	TestTrue(TEXT("Cancellation has no storage/corruption error"), Fixture.Progress->Error.IsEmpty());
	TestTrue(TEXT("The completed first file is already verified"), Fixture.Matches(Fixture.Games / TEXT("Disc A/Track.bin"), 17, 0x12));
	TestFalse(TEXT("The incomplete second disc has no final filename"), Fixture.Exists(Fixture.Games / TEXT("Disc B/Track.bin")));
	TestTrue(TEXT("Interruption leaves an abandoned partial for recovery"), Fixture.Exists(Fixture.Games / TEXT("Disc B/Track.bin.partial")));
	TestTrue(TEXT("Cancellation occurs after one chunk, before the second disc completes"), NativeFileMatches(Fixture.Games / TEXT("Disc B/Track.bin.partial"), 1024 * 1024));
	TestFalse(TEXT("Interrupted preparation is not ready"), Fixture.Exists(Fixture.Stamp()));
	std::error_code Error;
	const auto FirstTime = std::filesystem::last_write_time(NativePath(Fixture.Games / TEXT("Disc A/Track.bin")), Error);
	std::filesystem::remove(NativePath(Fixture.Source / TEXT("Disc A/Track.bin")), Error);
	if (!Fixture.Write(Fixture.Games / TEXT("Disc B/Track.bin.verified.partial"), {0x41})) return false;
	FPreparationServices Resume;
	Resume.FreeSpace = [this, &Fixture](const FString&, uint64& Bytes)
	{
		TestFalse(TEXT("Abandoned data is removed before measuring storage"), Fixture.Exists(Fixture.Games / TEXT("Disc B/Track.bin.partial")));
		TestFalse(TEXT("Abandoned verification metadata is removed before measuring storage"), Fixture.Exists(Fixture.Games / TEXT("Disc B/Track.bin.verified.partial")));
		Bytes = BundleStorageReserve + LargeSize;
		return true;
	};
	TestTrue(TEXT("Retry needs only the remaining disc's space and source"), Fixture.Run(Resume));
	TestTrue(TEXT("Completed file is reused without rewriting"), FirstTime == std::filesystem::last_write_time(NativePath(Fixture.Games / TEXT("Disc A/Track.bin")), Error));
	TestTrue(TEXT("The resumed second disc matches every generated byte"), Fixture.Matches(Fixture.Games / TEXT("Disc B/Track.bin"), LargeSize, 0x75));
	TestEqual(TEXT("Reused plus copied progress equals the full bundle"), Fixture.Progress->Done.load(), uint64(LargeSize + 17));
	TestTrue(TEXT("Only a completed retry publishes readiness"), NativeMarkerMatches(Fixture.Stamp(), HashText(Fixture.Text)));
	TestFalse(TEXT("Successful recovery leaves no partial data"), Fixture.Exists(Fixture.Games / TEXT("Disc B/Track.bin.partial")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleCorruptSourceTest, "Weltall.Storage.BundledDiscs.CorruptOrMissingSource", BundleTestFlags)
bool FBundleCorruptSourceTest::RunTest(const FString&)
{
	FBundlePreparationFixture Fixture;
	if (!Fixture.Add(TEXT("Disc A/Track.bin"), 19, 0x42)) return false;
	TArray<uint8> Corrupt;
	Corrupt.Init(0x43, 19);
	if (!Fixture.Write(Fixture.Source / TEXT("Disc A/Track.bin"), Corrupt)) return false;
	AddExpectedError(TEXT("Bundled game verification failed"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("An equal-size source with different bytes fails its digest"), Fixture.Run(BundleSpace(MAX_uint64)));
	TestFalse(TEXT("Corrupt bytes are not published"), Fixture.Exists(Fixture.Games / TEXT("Disc A/Track.bin")));
	TestFalse(TEXT("A failed digest never marks readiness"), Fixture.Exists(Fixture.Stamp()));
	std::error_code Error;
	std::filesystem::remove(NativePath(Fixture.Source / TEXT("Disc A/Track.bin")), Error);
	AddExpectedError(TEXT("Bundled game files could not be opened"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Missing packaged source produces an actionable failure"), Fixture.Run(BundleSpace(MAX_uint64)));
	TestFalse(TEXT("Missing source does not mark readiness"), Fixture.Exists(Fixture.Stamp()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleStaleFileTest, "Weltall.Storage.BundledDiscs.IncompleteFilesAndIsolation", BundleTestFlags)
bool FBundleStaleFileTest::RunTest(const FString&)
{
	FBundlePreparationFixture Fixture;
	if (!Fixture.Add(TEXT("Disc A/Track.bin"), 17, 0x26)
		|| !Fixture.Write(Fixture.Games / TEXT("Disc A/Track.bin"), {0x00})
		|| !Fixture.Write(Fixture.Root / TEXT("Saves/keep.sav"), {0x51, 0x51})
		|| !Fixture.Write(Fixture.Root / TEXT("Settings/keep.txt"), {0x54})) return false;
	TestTrue(TEXT("An incomplete final file is replaced by verified bytes"), Fixture.Run(BundleSpace(BundleStorageReserve + 17)));
	TestTrue(TEXT("Replacement matches the generated source"), Fixture.Matches(Fixture.Games / TEXT("Disc A/Track.bin"), 17, 0x26));
	TestTrue(TEXT("Sibling generated save remains unchanged"), Fixture.Matches(Fixture.Root / TEXT("Saves/keep.sav"), 2, 0x51));
	TestTrue(TEXT("Sibling generated settings remain unchanged"), Fixture.Matches(Fixture.Root / TEXT("Settings/keep.txt"), 1, 0x54));
	// A malformed descriptor cannot point recovery/deletion at sibling player data.
	Fixture.Text = TEXT("WELTALL_BUNDLED_DISCS_V1\n") + FString::ChrN(40, TEXT('a')) + TEXT("|2|../../Saves/keep.sav\n");
	AddExpectedError(TEXT("Bundled game information is invalid"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("An escaping descriptor is rejected before the worker writes"), Fixture.Run(BundleSpace(MAX_uint64)));
	TestTrue(TEXT("Sibling save remains unchanged after malformed-input failure"), Fixture.Matches(Fixture.Root / TEXT("Saves/keep.sav"), 2, 0x51));
	TestTrue(TEXT("The previously prepared disc remains unchanged"), Fixture.Matches(Fixture.Games / TEXT("Disc A/Track.bin"), 17, 0x26));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBundleCancelledBeforeCopyTest, "Weltall.Storage.BundledDiscs.CancelledBeforeCopy", BundleTestFlags)
bool FBundleCancelledBeforeCopyTest::RunTest(const FString&)
{
	FBundlePreparationFixture Fixture;
	if (!Fixture.Add(TEXT("Disc A/Track.bin"), 17, 0x65)) return false;
	TArray<FBundledFile> BundleEntries;
	if (!ParseManifest(Fixture.Text, BundleEntries, Fixture.Progress->Total)) return false;
	// The same atomic flag Stop() sets when the app shuts down, before the worker opens a source.
	Fixture.Progress->Cancelled = true;
	TestFalse(TEXT("Shutdown cancellation prevents copying"), Prepare(BundleEntries, Fixture.Source, Fixture.Games,
		HashText(Fixture.Text), Fixture.Progress, BundleSpace(MAX_uint64)));
	TestEqual(TEXT("No source bytes are copied after initial cancellation"), Fixture.Progress->Done.load(), uint64(0));
	TestFalse(TEXT("Cancelled job has no partial disc"), Fixture.Exists(Fixture.Games / TEXT("Disc A/Track.bin.partial")));
	TestFalse(TEXT("Cancelled job does not publish readiness"), Fixture.Exists(Fixture.Stamp()));
	TestTrue(TEXT("A fresh job after shutdown can complete"), Fixture.Run(BundleSpace(MAX_uint64)));
	return true;
}
