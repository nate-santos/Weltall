#include "Game/SaveStore.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Weltall;

namespace
{
	/** A fresh folder for a store, removed when the test ends. */
	struct FStoreFolder
	{
		std::filesystem::path Path;

		explicit FStoreFolder(const char* Name) : Path(std::filesystem::temp_directory_path() / Name)
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}

		~FStoreFolder()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}
	};

	FOriginalSave MakeSave(uint8 Mark)
	{
		std::vector<uint8> Data(FOriginalSave::DataSize, 0);
		Data[0] = Mark;
		return FOriginalSave::WithChecksum(std::move(Data));
	}
}

TEST_CASE("The save store keeps each slot's save as its own file", "[Game]")
{
	const FStoreFolder Folder("WeltallSaveStoreTest");
	FSaveStore Store(Folder.Path);
	const auto Empty = Store.Read(0);
	REQUIRE(Empty);
	CHECK_FALSE(Empty->has_value());
	CHECK_FALSE(Store.LastSaved().has_value());

	CHECK_FALSE(Store.Write(0, MakeSave(1)));
	CHECK_FALSE(Store.Write(119, MakeSave(2)));
	CHECK(std::filesystem::exists(Folder.Path / "Card1" / "No01.sav"));
	CHECK(std::filesystem::exists(Folder.Path / "Card8" / "No15.sav"));
	CHECK(Store.LastSaved() == 119);

	// Another store over the same folder reads what the first wrote.
	const FSaveStore Again(Folder.Path);
	const auto First = Again.Read(0);
	REQUIRE(First);
	REQUIRE(First->has_value());
	CHECK((*First)->Data()[0] == 1);
	CHECK(Again.LastSaved() == 119);

	CHECK_FALSE(Store.Write(0, MakeSave(3)));
	CHECK((*Store.Read(0))->Data()[0] == 3);
	CHECK(Store.LastSaved() == 0);
	CHECK_FALSE(std::filesystem::exists(Folder.Path / "Card1" / "No01.sav.new"));

	CHECK_FALSE(Store.Remove(0));
	CHECK_FALSE(Store.Read(0)->has_value());
	// The slot last saved to is forgotten once its save is gone.
	CHECK_FALSE(Store.LastSaved().has_value());
	CHECK_FALSE(Store.Remove(0));
}

TEST_CASE("The save store refuses slots it does not have and saves that are damaged", "[Game]")
{
	const FStoreFolder Folder("WeltallSaveStoreDamageTest");
	FSaveStore Store(Folder.Path);
	CHECK_FALSE(Store.Read(-1));
	CHECK_FALSE(Store.Read(120));
	CHECK(Store.Write(120, MakeSave(1)).has_value());
	CHECK(Store.Remove(-1).has_value());

	CHECK_FALSE(Store.Write(16, MakeSave(1)));
	{
		std::fstream File(Folder.Path / "Card2" / "No02.sav", std::ios::binary | std::ios::in | std::ios::out);
		File.seekp(5);
		File.put('\x7F');
	}
	CHECK_FALSE(Store.Read(16));
}

TEST_CASE("An abandoned temporary save never hides the committed slot", "[Game][SaveFailure]")
{
	const FStoreFolder Folder("WeltallSaveAbandonedTemporaryTest");
	FSaveStore Store(Folder.Path);
	REQUIRE_FALSE(Store.Write(0, MakeSave(1)));
	const std::filesystem::path Temporary = Folder.Path / "Card1/No01.sav.new";
	{
		std::ofstream File(Temporary, std::ios::binary);
		File << "incomplete generated fixture";
	}
	const FSaveStore Reopened(Folder.Path);
	const auto Committed = Reopened.Read(0);
	REQUIRE(Committed);
	REQUIRE(Committed->has_value());
	CHECK(std::ranges::equal((**Committed).Data(), MakeSave(1).Data()));
	CHECK(Reopened.LastSaved() == 0);
	REQUIRE_FALSE(Store.Write(0, MakeSave(2)));
	const auto Retried = Store.Read(0);
	REQUIRE(Retried);
	REQUIRE(Retried->has_value());
	CHECK(std::ranges::equal((**Retried).Data(), MakeSave(2).Data()));
	CHECK_FALSE(std::filesystem::exists(Temporary));
}

TEST_CASE("Failure to open a temporary save preserves the prior slot and last-save pointer", "[Game][SaveFailure]")
{
	const FStoreFolder Folder("WeltallSaveTemporaryOpenFailureTest");
	FSaveStore Store(Folder.Path);
	REQUIRE_FALSE(Store.Write(0, MakeSave(1)));
	REQUIRE_FALSE(Store.Write(119, MakeSave(2)));
	const std::filesystem::path Temporary = Folder.Path / "Card1/No01.sav.new";
	REQUIRE(std::filesystem::create_directory(Temporary));
	CHECK(Store.Write(0, MakeSave(3)).has_value());
	const FSaveStore Reopened(Folder.Path);
	const auto Committed = Reopened.Read(0);
	REQUIRE(Committed);
	REQUIRE(Committed->has_value());
	CHECK(std::ranges::equal((**Committed).Data(), MakeSave(1).Data()));
	CHECK(Reopened.LastSaved() == 119);
	const auto Other = Reopened.Read(119);
	REQUIRE(Other);
	REQUIRE(Other->has_value());
	CHECK(std::ranges::equal((**Other).Data(), MakeSave(2).Data()));
}
