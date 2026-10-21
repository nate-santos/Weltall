#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <utility>
#include <vector>

using namespace Weltall;

namespace
{
	void CheckBootFiles(const FIso9660& Disc, const char* Serial)
	{
		// Everything else on the disc is reached through the game's own file index.
		REQUIRE(Disc.Entries().size() == 2);
		CHECK(Disc.Find("SYSTEM.CNF") != nullptr);
		const auto Executable = ReadBootExecutable(Disc);
		REQUIRE(Executable);
		CHECK(*FindBootExecutable(Disc) == Serial);
		CHECK(Executable->LoadAddress() == 0x80010000);
		CHECK(Executable->InitialPc() == 0x80019524);
		CHECK(Executable->Body().size() == 0x49800);
	}
}

TEST_CASE("Disc 1 is identified and holds only its boot files", "[Media][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const auto Identity = IdentifyDisc(*Disc);
	REQUIRE(Identity);
	CHECK(Identity->DiscNumber == 1);
	CheckBootFiles(*Disc, "SLUS_006.64");
}

TEST_CASE("Disc 2 is identified and holds only its boot files", "[Media][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc2);
	const auto Identity = IdentifyDisc(*Disc);
	REQUIRE(Identity);
	CHECK(Identity->DiscNumber == 2);
	CheckBootFiles(*Disc, "SLUS_006.69");
}

TEST_CASE("A disc whose program differs only where a patch of the game's data rewrites it is the release, patched", "[Media][Content]")
{
	for (const auto& [Release, Name] : {std::pair{EDiscRelease::UsaDisc1, "SLUS_006.64"}, std::pair{EDiscRelease::UsaDisc2, "SLUS_006.69"}})
	{
		REQUIRE_DISC(Disc, Release);
		const auto Program = Disc->ReadFile(Name);
		REQUIRE(Program);
		const auto At = [](uint32 Address) { return FPsxExecutable::HeaderSize + (Address - 0x80010000); };
		const auto Original = IdentifyExecutable(Name, *Program);
		REQUIRE(Original);
		CHECK(Original->Release == Release);
		CHECK_FALSE(Original->bPatched);
		// A translation moves and resizes files (their index records) and redraws the boot notice's picture.
		std::vector<uint8> Translated = *Program;
		Translated[At(0x80011000)] ^= 0xFF;
		Translated[At(0x80018003)] ^= 0xFF;
		Translated[At(0x8004EABC)] ^= 0xFF;
		Translated.back() ^= 0xFF;
		const auto Patched = IdentifyExecutable(Name, Translated);
		REQUIRE(Patched);
		CHECK(Patched->Release == Release);
		CHECK(Patched->DiscNumber == Original->DiscNumber);
		CHECK(Patched->bPatched);
		// Its code changed, or the directory table after the index, it is a release Weltall does not know.
		std::vector<uint8> Coded = Translated;
		Coded[At(0x80019524)] ^= 0x01;
		CHECK_FALSE(IdentifyExecutable(Name, Coded));
		std::vector<uint8> Directories = Translated;
		Directories[At(0x80018004)] ^= 0xFF;
		CHECK_FALSE(IdentifyExecutable(Name, Directories));
		CHECK_FALSE(IdentifyExecutable(Release == EDiscRelease::UsaDisc1 ? "SLUS_006.69" : "SLUS_006.64", Translated));
	}
}
