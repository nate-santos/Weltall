// Deterministic process-death checks around the production save store's libc operations.
// The linker wrappers belong only to this standalone Linux/Android test executable.
#include "Game/SaveStore.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace
{
	std::filesystem::path Root;
	std::string Temporary, MetadataTemporary, Phase;
	bool Stopped = false;
	constexpr std::string_view Identity = "WELTALL_SAVE_INTERRUPTION_V1\n";
	[[noreturn]] void Fail(const char* Message)
	{
		std::fprintf(stderr, "SAVE_PROBE_ERROR %s (errno=%d)\n", Message, errno);
		std::exit(1);
	}

	bool IsTarget(FILE* File)
	{
		std::array<char, 4096> Path{};
		const std::string Link = "/proc/self/fd/" + std::to_string(fileno(File));
		const ssize_t Count = readlink(Link.c_str(), Path.data(), Path.size() - 1);
		return Count >= 0 && std::string_view(Path.data(), static_cast<std::size_t>(Count)) == Temporary;
	}

	void Barrier(std::string_view Stage)
	{
		if (Stopped || Stage != Phase) return;
		Stopped = true;
		const std::string Text = std::string(Stage) + " " + std::to_string(getpid()) + "\n";
		const int File = open((Root / "barrier.txt").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (File < 0) Fail("cannot create fresh barrier marker");
		const ssize_t Count = write(File, Text.data(), Text.size());
		const bool Flushed = fsync(File) == 0;
		close(File);
		if (Count != static_cast<ssize_t>(Text.size()) || !Flushed) Fail("cannot publish barrier marker");
		std::printf("SAVE_BARRIER %s", Text.c_str());
		std::fflush(stdout);
		// The host verifies the stopped PID before SIGKILL, or SIGCONT for suspension/retry.
		raise(SIGSTOP);
	}

	Weltall::FOriginalSave GeneratedSave(Weltall::uint8 Mark)
	{
		std::vector<Weltall::uint8> Data(Weltall::FOriginalSave::DataSize, 0);
		Data[0] = Mark;
		return Weltall::FOriginalSave::WithChecksum(std::move(Data));
	}

	void Put(Weltall::FSaveStore& Store, Weltall::int32 Slot, Weltall::uint8 Mark)
	{
		if (const auto Error = Store.Write(Slot, GeneratedSave(Mark))) Fail(Error->Message.c_str());
	}

	void CheckSlot(const Weltall::FSaveStore& Store, Weltall::int32 Slot, Weltall::uint8 Mark)
	{
		const auto Save = Store.Read(Slot);
		if (!Save || !Save->has_value() || !std::ranges::equal((**Save).Data(), GeneratedSave(Mark).Data())) Fail("saved bytes/checksum differ");
	}
}

extern "C" std::size_t __real_fwrite(const void*, std::size_t, std::size_t, FILE*);
extern "C" int __real_fclose(FILE*);
extern "C" int __real_rename(const char*, const char*);

extern "C" std::size_t __wrap_fwrite(const void* Bytes, std::size_t Size, std::size_t Count, FILE* File)
{
	const bool Target = IsTarget(File);
	if (Target) Barrier("before-write");
	const std::size_t Result = __real_fwrite(Bytes, Size, Count, File);
	if (Target) Barrier("after-write");
	return Result;
}

extern "C" int __wrap_fclose(FILE* File)
{
	const bool Target = IsTarget(File);
	if (Target) Barrier("before-close");
	const int Result = __real_fclose(File);
	if (Target && Result == 0) Barrier("after-close");
	return Result;
}

extern "C" int __wrap_rename(const char* From, const char* To)
{
	const bool Target = Temporary == From;
	const bool Metadata = MetadataTemporary == From;
	if (Target) Barrier("before-rename");
	if (Metadata) Barrier("before-metadata-rename");
	const int Result = __real_rename(From, To);
	if (Target && Result == 0) Barrier("after-rename");
	if (Metadata && Result == 0) Barrier("after-metadata-rename");
	return Result;
}

int main(int Argc, char** Argv)
{
	if (Argc < 3) Fail("usage: prepare|write|verify ROOT [PHASE|MARK LAST_SLOT]");
	Root = std::filesystem::path(Argv[2]);
	if (!Root.is_absolute() || Root != Root.lexically_normal() || !Root.filename().string().starts_with("weltall-save-probe-"))
		Fail("root must be an absolute, normalized, explicitly named test directory");
	Temporary = (Root / "Card8/No15.sav.new").string();
	MetadataTemporary = (Root / "LastSaved.txt.new").string();
	const std::string_view Command(Argv[1]);
	if (Command == "prepare")
	{
		std::error_code Error;
		if (!std::filesystem::create_directory(Root, Error) || Error) Fail("test directory must be new");
		{
			std::ofstream File(Root / "probe-id.txt", std::ios::binary);
			File << Identity;
			File.close();
			if (!File) Fail("cannot identify new test directory");
		}
		Weltall::FSaveStore Store(Root);
		Put(Store, 119, 0x22);
		Put(Store, 0, 0x11);
		std::puts("PREPARE_OK");
		return 0;
	}
	{
		std::ifstream File(Root / "probe-id.txt", std::ios::binary);
		const std::string Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		if (Text != Identity) Fail("directory was not prepared by this probe");
	}
	Weltall::FSaveStore Store(Root);
	if (Command == "write" && Argc == 4)
	{
		Phase = Argv[3];
		Put(Store, 119, 0x33);
		if (!Stopped && Phase != "none") Fail("requested libc boundary was not observed");
		CheckSlot(Store, 119, 0x33);
		if (Store.LastSaved() != 119) Fail("completed save did not update its pointer");
		std::puts("WRITE_OK");
		return 0;
	}
	if (Command == "verify" && Argc == 5)
	{
		const int Mark = std::atoi(Argv[3]), Last = std::atoi(Argv[4]);
		if ((Mark != 0x22 && Mark != 0x33) || (Last != 0 && Last != 119)) Fail("invalid expected test values");
		CheckSlot(Store, 0, 0x11);
		CheckSlot(Store, 119, static_cast<Weltall::uint8>(Mark));
		if (Store.LastSaved() != Last) Fail("last-save pointer differs");
		std::puts("VERIFY_OK");
		return 0;
	}
	Fail("unknown probe command");
}
